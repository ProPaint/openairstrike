// Exports functions.json, strings.json, imports.json, data_tables.json, per-function
// decompiled/*.c + all.c + decompile_errors.txt, per-function disasm/*.asm, and SUMMARY.md
// for the currently loaded program.
//
// Usage (headless): -postScript ExportAll.java <out_dir> <probe.json>
// <probe.json> is re/probes/<tag>.json: builtin/global name probes, acceptance strings, expected
// table locations (see re/README.md).
//
// Run RenameByErrorStrings.java first (as an earlier -postScript) if you want the naming
// pass reflected in the exported names.
//@category AirStrike3D

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

import com.google.gson.*;
import com.google.gson.stream.JsonWriter;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.util.task.TaskMonitor;

public class ExportAll extends GhidraScript {

	private static final int DECOMPILE_TIMEOUT_SECS = 60;

	private FunctionManager fm;
	private Listing listing;

	// address -> string text, for every defined string in the program
	private final Map<Address, String> stringText = new TreeMap<>();
	// string address -> set of function entry points referencing it
	private final Map<Address, Set<Address>> stringRefs = new TreeMap<>();
	// function entry -> list of string addresses it references
	private final Map<Address, List<Address>> funcStrings = new TreeMap<>();
	// function entry -> set of all referenced data addresses (string + non-string)
	private final Map<Address, Set<Address>> funcDataRefs = new TreeMap<>();
	// function entry -> Function, cached for ordering
	private final Map<Address, Function> allFuncs = new TreeMap<>();

	private File outDir, decompDir, disasmDir;

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 2) {
			printerr("ExportAll: expected <out_dir> <probe.json> arguments");
			return;
		}
		loadProbes(new File(args[1]));
		outDir = new File(args[0]);
		decompDir = new File(outDir, "decompiled");
		disasmDir = new File(outDir, "disasm");
		outDir.mkdirs();
		decompDir.mkdirs();
		disasmDir.mkdirs();

		fm = currentProgram.getFunctionManager();
		listing = currentProgram.getListing();

		println("ExportAll: pre-pass: creating functions for builtin-table entries Ghidra " +
			"missed (code only reachable via a data-table function pointer)...");
		discoverBuiltinTableFunctions();

		println("ExportAll: collecting functions...");
		FunctionIterator fit = listing.getFunctions(true);
		while (fit.hasNext()) {
			if (monitor.isCancelled()) return;
			Function f = fit.next();
			allFuncs.put(f.getEntryPoint(), f);
		}
		println("ExportAll: " + allFuncs.size() + " functions total");

		println("ExportAll: collecting strings...");
		collectStrings();
		println("ExportAll: " + stringText.size() + " strings");

		println("ExportAll: collecting per-function data/string references...");
		collectFunctionReferences();

		println("ExportAll: writing strings.json...");
		writeStringsJson();

		println("ExportAll: writing functions.json...");
		writeFunctionsJson();

		println("ExportAll: writing imports.json...");
		writeImportsJson();

		println("ExportAll: writing data_tables.json...");
		writeDataTablesJson();

		println("ExportAll: decompiling and disassembling functions...");
		decompileAndDisassemble();

		println("ExportAll: writing SUMMARY.md...");
		writeSummary();

		println("ExportAll: done. Output in " + outDir.getAbsolutePath());
	}

	// ---------------------------------------------------------------- strings

	private void collectStrings() {
		DataIterator dataIt = listing.getDefinedData(true);
		while (dataIt.hasNext()) {
			if (monitor.isCancelled()) return;
			Data data = dataIt.next();
			if (!StringDataInstance.isString(data)) {
				continue;
			}
			StringDataInstance sdi = StringDataInstance.getStringDataInstance(data);
			String text = sdi.getStringValue();
			if (text == null) {
				continue;
			}
			Address addr = data.getAddress();
			stringText.put(addr, text);
			Set<Address> funcs = new TreeSet<>();
			for (Reference ref : getReferencesTo(addr)) {
				Function f = fm.getFunctionContaining(ref.getFromAddress());
				if (f != null) {
					funcs.add(f.getEntryPoint());
				}
			}
			stringRefs.put(addr, funcs);
		}
	}

	private void writeStringsJson() throws IOException {
		Gson gson = new GsonBuilder().setPrettyPrinting().create();
		try (JsonWriter jw = new JsonWriter(new OutputStreamWriter(
			new FileOutputStream(new File(outDir, "strings.json")), StandardCharsets.UTF_8))) {
			jw.setIndent("  ");
			jw.beginArray();
			for (Map.Entry<Address, String> e : stringText.entrySet()) {
				jw.beginObject();
				jw.name("address").value(addrStr(e.getKey()));
				jw.name("text").value(e.getValue());
				jw.name("functions");
				jw.beginArray();
				for (Address fa : stringRefs.getOrDefault(e.getKey(), Collections.emptySet())) {
					jw.value(addrStr(fa));
				}
				jw.endArray();
				jw.endObject();
			}
			jw.endArray();
		}
	}

	// --------------------------------------------------- per-function references

	private void collectFunctionReferences() {
		// Invert stringRefs -> funcStrings, and separately walk every instruction in every
		// function body to gather non-flow (data) references, classifying string vs other.
		for (Map.Entry<Address, Set<Address>> e : stringRefs.entrySet()) {
			Address strAddr = e.getKey();
			for (Address funcEntry : e.getValue()) {
				funcStrings.computeIfAbsent(funcEntry, k -> new ArrayList<>()).add(strAddr);
				funcDataRefs.computeIfAbsent(funcEntry, k -> new TreeSet<>()).add(strAddr);
			}
		}

		int count = 0;
		for (Function f : allFuncs.values()) {
			if (monitor.isCancelled()) return;
			count++;
			if (count % 500 == 0) {
				println("  ... references for " + count + "/" + allFuncs.size());
			}
			if (f.isExternal()) {
				continue;
			}
			AddressSetView body = f.getBody();
			InstructionIterator ii = listing.getInstructions(body, true);
			while (ii.hasNext()) {
				Instruction instr = ii.next();
				Reference[] refs = instr.getReferencesFrom();
				for (Reference ref : refs) {
					RefType rt = ref.getReferenceType();
					if (rt.isFlow()) {
						continue; // calls/jumps handled separately via callers/callees
					}
					Address to = ref.getToAddress();
					if (body.contains(to)) {
						continue; // internal branch/data within same function (e.g. jump table)
					}
					funcDataRefs.computeIfAbsent(f.getEntryPoint(), k -> new TreeSet<>()).add(to);
				}
			}
		}
	}

	// --------------------------------------------------------------- functions.json

	private void writeFunctionsJson() throws IOException {
		try (JsonWriter jw = new JsonWriter(new OutputStreamWriter(
			new FileOutputStream(new File(outDir, "functions.json")), StandardCharsets.UTF_8))) {
			jw.setIndent("  ");
			jw.beginArray();
			int count = 0;
			for (Function f : allFuncs.values()) {
				if (monitor.isCancelled()) return;
				count++;
				if (count % 500 == 0) {
					println("  ... functions.json " + count + "/" + allFuncs.size());
				}
				Address entry = f.getEntryPoint();
				jw.beginObject();
				jw.name("entry").value(addrStr(entry));
				jw.name("name").value(f.getName());
				jw.name("size").value(f.getBody().getNumAddresses());
				String cc = f.getCallingConventionName();
				jw.name("calling_convention").value(cc == null ? "" : cc);
				jw.name("is_thunk").value(f.isThunk());
				jw.name("is_external").value(f.isExternal());

				jw.name("callers");
				jw.beginArray();
				Set<Function> callers = f.getCallingFunctions(monitor);
				List<Address> callerAddrs = new ArrayList<>();
				for (Function c : callers) callerAddrs.add(c.getEntryPoint());
				Collections.sort(callerAddrs);
				for (Address a : callerAddrs) jw.value(addrStr(a));
				jw.endArray();

				jw.name("callees");
				jw.beginArray();
				Set<Function> callees = f.getCalledFunctions(monitor);
				List<Function> calleeList = new ArrayList<>(callees);
				calleeList.sort(Comparator.comparing(Function::getEntryPoint));
				for (Function c : calleeList) {
					jw.beginObject();
					jw.name("address").value(addrStr(c.getEntryPoint()));
					jw.name("name").value(c.getName());
					jw.endObject();
				}
				jw.endArray();

				jw.name("strings");
				jw.beginArray();
				List<Address> strs = new ArrayList<>(funcStrings.getOrDefault(entry, Collections.emptyList()));
				Collections.sort(strs);
				for (Address sa : strs) {
					jw.beginObject();
					jw.name("address").value(addrStr(sa));
					jw.name("text").value(stringText.get(sa));
					jw.endObject();
				}
				jw.endArray();

				jw.name("data_refs");
				jw.beginArray();
				List<Address> drefs = new ArrayList<>(funcDataRefs.getOrDefault(entry, Collections.emptySet()));
				Collections.sort(drefs);
				for (Address da : drefs) jw.value(addrStr(da));
				jw.endArray();

				jw.endObject();
			}
			jw.endArray();
		}
	}

	// ----------------------------------------------------------------- imports.json

	private void writeImportsJson() throws IOException {
		// dll name -> list of {name, thunk_stub_addresses, iat_slot_address, callers}
		//
		// Most imports here are called via a direct indirect "CALL DWORD PTR [iat_slot]" with
		// no separate jump-stub function in .text; Ghidra represents each import as a Function
		// living in the fake EXTERNAL address space (small addresses like 0x00000010), and
		// resolves such indirect calls straight to it, so iterating fm.getExternalFunctions()
		// (rather than scanning for isThunk() wrapper functions, which only exist for a
		// minority of imports on this binary) is what actually finds every import and every
		// caller. ExternalLocation.getAddress() turned out to return a synthetic value rather
		// than the real IAT pointer for most entries, so the real .rdata/.data IAT slot address
		// (when Ghidra created a Data pointer for it) is instead found by walking references
		// TO the external function's entry and picking the one that lands on a Data item
		// rather than an instruction.
		Map<String, List<JsonObject>> byDll = new TreeMap<>();

		FunctionIterator extIt = fm.getExternalFunctions();
		while (extIt.hasNext()) {
			if (monitor.isCancelled()) break;
			Function extFn = extIt.next();

			String dll = "UNKNOWN";
			try {
				if (extFn.getExternalLocation() != null &&
					extFn.getExternalLocation().getLibraryName() != null) {
					dll = extFn.getExternalLocation().getLibraryName();
				}
			}
			catch (Exception ex) {
				// leave dll as UNKNOWN
			}

			Set<Address> callerAddrs = new TreeSet<>();
			Address iatSlotAddr = null;
			Set<Address> thunkStubAddrs = new TreeSet<>();
			try {
				for (Reference ref : getReferencesTo(extFn.getEntryPoint())) {
					Address from = ref.getFromAddress();
					if (listing.getInstructionAt(from) != null) {
						Function containing = fm.getFunctionContaining(from);
						if (containing == null) continue;
						if (containing.isThunk() &&
							extFn.equals(containing.getThunkedFunction(true))) {
							// 'containing' is itself just a jump-stub to this import; its real
							// callers (not the stub) are the interesting addresses.
							thunkStubAddrs.add(containing.getEntryPoint());
							for (Function c : containing.getCallingFunctions(monitor)) {
								callerAddrs.add(c.getEntryPoint());
							}
						}
						else {
							callerAddrs.add(containing.getEntryPoint());
						}
					}
					else if (iatSlotAddr == null && listing.getDataAt(from) != null) {
						iatSlotAddr = from;
					}
				}
			}
			catch (Exception ex) {
				// leave whatever was collected so far
			}

			JsonObject entry = new JsonObject();
			entry.addProperty("name", extFn.getName());
			JsonArray thunkArr = new JsonArray();
			for (Address ta : thunkStubAddrs) thunkArr.add(addrStr(ta));
			entry.add("thunk_stub_addresses", thunkArr);
			entry.addProperty("iat_slot_address", iatSlotAddr == null ? null : addrStr(iatSlotAddr));

			JsonArray callersArr = new JsonArray();
			for (Address a : callerAddrs) callersArr.add(addrStr(a));
			entry.add("callers", callersArr);

			byDll.computeIfAbsent(dll, k -> new ArrayList<>()).add(entry);
		}

		JsonObject root = new JsonObject();
		for (Map.Entry<String, List<JsonObject>> e : byDll.entrySet()) {
			JsonArray arr = new JsonArray();
			// sort by name for stable output
			e.getValue().sort(Comparator.comparing(o -> o.get("name").getAsString()));
			for (JsonObject o : e.getValue()) arr.add(o);
			root.add(e.getKey(), arr);
		}

		Gson gson = new GsonBuilder().setPrettyPrinting().create();
		try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(outDir, "imports.json")),
			StandardCharsets.UTF_8)) {
			gson.toJson(root, w);
		}
	}

	// ------------------------------------------------------------- data_tables.json

	// Known names to look for, to flag interesting tables specially.
	// Loaded from the probe file (re/probes/<tag>.json), see loadProbes().
	private String[] BUILTIN_NAMES = {};
	private String[] GLOBAL_NAMES = {};
	private String[] ACCEPTANCE_STRINGS = {};
	private String probeTag = "?";
	private String probeTitle = "?";
	private JsonObject probeExpected = new JsonObject();
	// every table found by writeDataTablesJson(), for the probe results in SUMMARY.md
	private JsonArray foundTables = new JsonArray();

	private static String[] jsonStrings(JsonObject o, String key) {
		JsonArray a = o.getAsJsonArray(key);
		String[] r = new String[a.size()];
		for (int i = 0; i < r.length; i++) r[i] = a.get(i).getAsString();
		return r;
	}

	private void loadProbes(File f) throws IOException {
		JsonObject o;
		try (Reader r = new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8)) {
			o = JsonParser.parseReader(r).getAsJsonObject();
		}
		probeTag = o.get("tag").getAsString();
		probeTitle = o.get("title").getAsString();
		BUILTIN_NAMES = jsonStrings(o, "builtin_names");
		GLOBAL_NAMES = jsonStrings(o, "global_names");
		ACCEPTANCE_STRINGS = jsonStrings(o, "acceptance_strings");
		if (o.has("expected")) probeExpected = o.getAsJsonObject("expected");
		println("ExportAll: probes " + f + ": tag=" + probeTag + ", " + BUILTIN_NAMES.length +
			" builtin names, " + GLOBAL_NAMES.length + " global names");
	}

	/** Ghidra only turns bytes into a Function when it can see how they're reached (control
	 * flow, or an explicit function-start pattern search). Code that is only reached
	 * indirectly through a data table of function pointers -- like the SL_GetExternFunc
	 * builtin table -- is often left as plain undefined bytes. This does an early,
	 * function-collection-independent table scan (fm.getFunctionAt lookups just miss during
	 * this pass, which is fine -- it only needs the *string* side of each record) to find the
	 * builtin table specifically, then calls createFunction() at every entry whose second
	 * field is a plausible in-.text pointer without a function yet, so the later "real" export
	 * passes see accurate function_pointer entries and complete callers/callees/decompiles for
	 * every builtin implementation. */
	private void discoverBuiltinTableFunctions() {
		MemoryBlock textBlock = currentProgram.getMemory().getBlock(".text");
		if (textBlock == null) return;

		JsonArray earlyTables = new JsonArray();
		Memory mem = currentProgram.getMemory();
		for (MemoryBlock block : mem.getBlocks()) {
			String bn = block.getName();
			if (!(bn.equalsIgnoreCase(".data") || bn.equalsIgnoreCase(".rdata"))) continue;
			if (!block.isInitialized()) continue;
			scanBlockForTables(block, 8, earlyTables);
		}

		List<Address> toCreate = new ArrayList<>();
		for (int i = 0; i < earlyTables.size(); i++) {
			JsonObject table = earlyTables.get(i).getAsJsonObject();
			if (!"builtin_function_table".equals(table.get("kind_guess").getAsString())) continue;
			for (var entryEl : table.getAsJsonArray("entries")) {
				JsonObject entry = entryEl.getAsJsonObject();
				JsonElement kind = entry.get("second_field_kind");
				if (kind == null || !"integer_or_unknown".equals(kind.getAsString())) continue;
				JsonElement valEl = entry.get("second_field_value");
				if (valEl == null) continue;
				long v;
				try {
					v = valEl.getAsLong();
				}
				catch (Exception ex) {
					continue;
				}
				Address target = toAddrOrNull(v);
				if (target == null || !textBlock.contains(target)) continue;
				if (fm.getFunctionAt(target) != null) continue;
				toCreate.add(target);
			}
		}

		if (toCreate.isEmpty()) {
			println("ExportAll: no missing builtin-table function targets found");
			return;
		}

		int created = 0, failed = 0;
		int tx = currentProgram.startTransaction("ExportAll: create builtin functions");
		try {
			for (Address a : toCreate) {
				try {
					if (fm.getFunctionAt(a) != null) continue; // created via overlap already
					Function nf = createFunction(a, null);
					if (nf != null) created++;
					else failed++;
				}
				catch (Exception ex) {
					failed++;
				}
			}
		}
		finally {
			currentProgram.endTransaction(tx, true);
		}
		println("ExportAll: builtin-table function discovery: created " + created +
			", failed " + failed + " (of " + toCreate.size() + " candidates)");
	}

	private void writeDataTablesJson() throws IOException {
		JsonObject root = new JsonObject();
		JsonArray tables = new JsonArray();

		Memory mem = currentProgram.getMemory();
		for (MemoryBlock block : mem.getBlocks()) {
			String bn = block.getName();
			if (!(bn.equalsIgnoreCase(".data") || bn.equalsIgnoreCase(".rdata"))) {
				continue;
			}
			if (!block.isInitialized()) continue;
			// 8-byte stride catches {name*, second_field} tables like the builtin function
			// table; 12-byte stride catches {name*, field, field} tables like the global
			// variable name table (name pointer + two runtime-address fields).
			scanBlockForTables(block, 8, tables);
			scanBlockForTables(block, 12, tables);
		}

		root.add("tables", tables);
		foundTables = tables;

		// Targeted lookup: where do the known builtin-function-name and global-variable-name
		// strings live, and what (if anything) directly references each?
		JsonArray builtinHits = lookupKnownStrings(BUILTIN_NAMES);
		JsonArray globalHits = lookupKnownStrings(GLOBAL_NAMES);
		root.add("builtin_name_string_hits", builtinHits);
		root.add("global_name_string_hits", globalHits);

		// SL_GetExternFunc anchor: the function(s) referencing the "not supported" message.
		JsonArray anchor = new JsonArray();
		for (Map.Entry<Address, String> e : stringText.entrySet()) {
			if (e.getValue().contains("Built-in function") && e.getValue().contains("not supported")) {
				JsonObject o = new JsonObject();
				o.addProperty("string_address", addrStr(e.getKey()));
				o.addProperty("text", e.getValue());
				JsonArray funcs = new JsonArray();
				for (Address fa : stringRefs.getOrDefault(e.getKey(), Collections.emptySet())) {
					funcs.add(addrStr(fa));
				}
				o.add("referencing_functions", funcs);
				anchor.add(o);
			}
		}
		root.add("sl_get_extern_func_anchor", anchor);

		Gson gson = new GsonBuilder().setPrettyPrinting().create();
		try (Writer w = new OutputStreamWriter(new FileOutputStream(new File(outDir, "data_tables.json")),
			StandardCharsets.UTF_8)) {
			gson.toJson(root, w);
		}
	}

	private JsonArray lookupKnownStrings(String[] names) {
		JsonArray arr = new JsonArray();
		for (String name : names) {
			for (Map.Entry<Address, String> e : stringText.entrySet()) {
				if (e.getValue().equals(name)) {
					JsonObject o = new JsonObject();
					o.addProperty("name", name);
					o.addProperty("string_address", addrStr(e.getKey()));
					JsonArray refs = new JsonArray();
					for (Reference ref : getReferencesTo(e.getKey())) {
						JsonObject r = new JsonObject();
						r.addProperty("from", addrStr(ref.getFromAddress()));
						Function f = fm.getFunctionContaining(ref.getFromAddress());
						r.addProperty("from_function", f == null ? null : addrStr(f.getEntryPoint()));
						refs.add(r);
					}
					o.add("references", refs);
					arr.add(o);
				}
			}
		}
		return arr;
	}

	/** Scans a memory block for runs of >=3 consecutive {ptr,ptr|int} records of the given
	 * stride (bytes), where the first field points to a defined string. Appends found runs
	 * as JSON table objects to {@code tables}. */
	private void scanBlockForTables(MemoryBlock block, int stride, JsonArray tables) {
		// Records are only 4-byte aligned, and a table's position relative to the block start
		// is arbitrary (a 12-byte table in the sequels sits at a phase of 4), so scan every
		// 4-byte phase of the stride. Phase 0 is scanned first and always emits; runs found at
		// other phases are dropped when they overlap something already emitted.
		List<long[]> emitted = new ArrayList<>();
		for (int phase = 0; phase < stride; phase += 4) {
			scanBlockPhase(block, stride, phase, tables, emitted);
		}
	}

	private void emitRun(MemoryBlock block, Address first, int count, int stride, int phase,
		JsonArray tables, List<long[]> emitted) {
		long lo = first.subtract(block.getStart());
		long hi = lo + (long) count * stride;
		if (phase != 0) {
			for (long[] r : emitted) {
				if (lo < r[1] && r[0] < hi) return;
			}
		}
		emitted.add(new long[] { lo, hi });
		emitTable(first, count, stride, tables);
	}

	private void scanBlockPhase(MemoryBlock block, int stride, int phase, JsonArray tables,
		List<long[]> emitted) {
		Address start = block.getStart();
		Address end = block.getEnd();
		long size = block.getSize();
		if (size < stride * 3L) return;

		int wordSize = 4;
		List<Address> runStart = new ArrayList<>();
		int runLen = 0;
		Address addr = start;
		long offset = phase;
		Address runFirstAddr = null;

		while (offset + stride <= size) {
			if (monitor.isCancelled()) return;
			Address rec = addr.add(offset);
			boolean isRecord = false;
			try {
				long v1 = readUInt32(rec);
				Address p1 = toAddrOrNull(v1);
				boolean p1IsString = p1 != null && resolvePointerString(p1) != null;
				if (p1IsString) {
					isRecord = true;
				}
			}
			catch (Exception ex) {
				isRecord = false;
			}

			if (isRecord) {
				if (runLen == 0) {
					runFirstAddr = rec;
				}
				runLen++;
			}
			else {
				if (runLen >= 3) {
					emitRun(block, runFirstAddr, runLen, stride, phase, tables, emitted);
				}
				runLen = 0;
			}
			offset += stride;
		}
		if (runLen >= 3) {
			emitRun(block, runFirstAddr, runLen, stride, phase, tables, emitted);
		}
	}

	private void emitTable(Address firstRec, int count, int stride, JsonArray tables) {
		JsonObject table = new JsonObject();
		table.addProperty("address", addrStr(firstRec));
		table.addProperty("entry_stride", stride);
		table.addProperty("entry_count", count);
		table.addProperty("entry_layout_guess", stride >= 12 ?
			"{char* name_ptr; void* second_field; void* third_field}" :
			"{char* name_ptr; void* second_field}");

		Set<String> foundNames = new HashSet<>();
		JsonArray entries = new JsonArray();
		for (int i = 0; i < count; i++) {
			Address rec = firstRec.add((long) i * stride);
			JsonObject e = new JsonObject();
			e.addProperty("record_address", addrStr(rec));
			try {
				long v1 = readUInt32(rec);
				Address p1 = toAddrOrNull(v1);
				String text = p1 == null ? null : resolvePointerString(p1);
				e.addProperty("string_address", p1 == null ? null : addrStr(p1));
				e.addProperty("string_text", text);
				e.addProperty("string_was_predefined", p1 != null && stringText.containsKey(p1));
				if (text != null) foundNames.add(text);

				long v2 = readUInt32(rec.add(4));
				Address p2 = toAddrOrNull(v2);
				Function fn = p2 == null ? null : fm.getFunctionAt(p2);
				String p2text = p2 == null ? null : resolvePointerString(p2);
				if (fn != null) {
					e.addProperty("second_field_kind", "function_pointer");
					e.addProperty("second_field_address", addrStr(p2));
					e.addProperty("second_field_name", fn.getName());
				}
				else if (p2text != null) {
					e.addProperty("second_field_kind", "string_pointer");
					e.addProperty("second_field_address", addrStr(p2));
					e.addProperty("second_field_text", p2text);
				}
				else {
					e.addProperty("second_field_kind", "integer_or_unknown");
					e.addProperty("second_field_value", v2);
				}

				if (stride >= 12) {
					long v3 = readUInt32(rec.add(8));
					e.addProperty("third_field_value", v3);
				}
			}
			catch (Exception ex) {
				e.addProperty("error", ex.getMessage());
			}
			entries.add(e);
		}
		table.add("entries", entries);

		int builtinMatches = 0;
		for (String n : BUILTIN_NAMES) if (foundNames.contains(n)) builtinMatches++;
		int globalMatches = 0;
		for (String n : GLOBAL_NAMES) if (foundNames.contains(n)) globalMatches++;

		if (builtinMatches >= 3) {
			table.addProperty("kind_guess", "builtin_function_table");
		}
		else if (globalMatches >= 3) {
			table.addProperty("kind_guess", "global_variable_name_table");
		}
		else {
			table.addProperty("kind_guess", "unknown");
		}

		tables.add(table);
	}

	/** Resolves the text at a candidate string-pointer address: prefers an already-defined
	 * string Data item, but falls back to a raw read of a short, printable, NUL-terminated
	 * byte run so that words Ghidra's default AsciiStringAnalyzer skipped (it requires a
	 * minimum length, so 4-letter words like "self" are often left undefined) are still
	 * recognised as strings for table-detection purposes. */
	private String resolvePointerString(Address p) {
		if (p == null) return null;
		String known = stringText.get(p);
		if (known != null) return known;
		try {
			Memory mem = currentProgram.getMemory();
			if (!mem.contains(p)) return null;
			StringBuilder sb = new StringBuilder();
			for (int i = 0; i < 64; i++) {
				byte b = mem.getByte(p.add(i));
				if (b == 0) {
					break;
				}
				int c = b & 0xFF;
				if (c < 0x20 || c > 0x7E) {
					return null; // not printable ASCII -> not a plausible raw string
				}
				sb.append((char) c);
			}
			if (sb.length() >= 2) {
				return sb.toString();
			}
		}
		catch (Exception ex) {
			// out of bounds / unreadable -> not a string
		}
		return null;
	}

	private long readUInt32(Address a) throws Exception {
		byte[] b = new byte[4];
		currentProgram.getMemory().getBytes(a, b);
		long v = 0;
		for (int i = 3; i >= 0; i--) {
			v = (v << 8) | (b[i] & 0xFFL);
		}
		return v;
	}

	private Address toAddrOrNull(long value) {
		if (value == 0) return null;
		try {
			Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
			if (currentProgram.getMemory().contains(a)) {
				return a;
			}
		}
		catch (Exception ex) {
			// fallthrough
		}
		return null;
	}

	// ------------------------------------------------------- decompile + disassemble

	private void decompileAndDisassemble() throws IOException {
		DecompInterface decomp = new DecompInterface();
		DecompileOptions options = new DecompileOptions();
		decomp.setOptions(options);
		if (!decomp.openProgram(currentProgram)) {
			printerr("ExportAll: failed to open program in decompiler: " + decomp.getLastMessage());
			return;
		}

		File allC = new File(outDir, "all.c");
		File errFile = new File(outDir, "decompile_errors.txt");
		int ok = 0, failed = 0, count = 0;

		try (PrintWriter allWriter = new PrintWriter(new FileWriter(allC));
			PrintWriter errWriter = new PrintWriter(new FileWriter(errFile))) {

			for (Function f : allFuncs.values()) {
				if (monitor.isCancelled()) break;
				count++;
				if (count % 200 == 0) {
					println("  ... decompiled/disasm " + count + "/" + allFuncs.size());
				}
				Address entry = f.getEntryPoint();
				String baseName = fileBase(entry, f.getName());

				// Disassembly (skip for pure external/no-body functions)
				if (!f.isExternal() && f.getBody().getNumAddresses() > 0) {
					writeDisasm(f, new File(disasmDir, baseName + ".asm"));
				}

				if (f.isExternal()) {
					continue; // nothing to decompile
				}

				DecompileResults res;
				try {
					res = decomp.decompileFunction(f, DECOMPILE_TIMEOUT_SECS, monitor);
				}
				catch (Exception ex) {
					errWriter.println(addrStr(entry) + "," + f.getName() + ",EXCEPTION: " + ex);
					failed++;
					continue;
				}

				if (res != null && res.decompileCompleted() && res.getDecompiledFunction() != null) {
					String c = res.getDecompiledFunction().getC();
					File cf = new File(decompDir, baseName + ".c");
					try (PrintWriter pw = new PrintWriter(new FileWriter(cf))) {
						pw.println("// " + addrStr(entry) + "  " + f.getName());
						pw.print(c);
					}
					allWriter.println("// ==== " + addrStr(entry) + "  " + f.getName() + " ====");
					allWriter.println(c);
					allWriter.println();
					ok++;
				}
				else {
					String reason;
					if (res == null) {
						reason = "null result";
					}
					else if (res.isTimedOut()) {
						reason = "TIMEOUT";
					}
					else if (res.isCancelled()) {
						reason = "CANCELLED";
					}
					else {
						reason = "ERROR: " + res.getErrorMessage();
					}
					errWriter.println(addrStr(entry) + "," + f.getName() + "," + reason.replace(",", ";").replace("\n", " "));
					failed++;
				}
			}
		}
		finally {
			decomp.dispose();
		}
		println("ExportAll: decompiled OK=" + ok + " FAILED=" + failed);
		decompOk = ok;
		decompFailed = failed;
	}

	private int decompOk = 0;
	private int decompFailed = 0;

	private void writeDisasm(Function f, File out) throws IOException {
		try (PrintWriter pw = new PrintWriter(new FileWriter(out))) {
			pw.println("; " + addrStr(f.getEntryPoint()) + "  " + f.getName() + "  size=" +
				f.getBody().getNumAddresses());
			InstructionIterator ii = listing.getInstructions(f.getBody(), true);
			while (ii.hasNext()) {
				Instruction instr = ii.next();
				byte[] bytes;
				try {
					bytes = instr.getBytes();
				}
				catch (Exception ex) {
					bytes = new byte[0];
				}
				StringBuilder hex = new StringBuilder();
				for (byte b : bytes) hex.append(String.format("%02x ", b));
				pw.printf("%-10s %-24s %s%n", addrStr(instr.getAddress()), hex.toString().trim(),
					instr.toString());
			}
		}
	}

	private static String fileBase(Address addr, String name) {
		String safe = name.replaceAll("[^A-Za-z0-9_.\\-]", "_");
		if (safe.length() > 80) safe = safe.substring(0, 80);
		return addrHex(addr) + "_" + safe;
	}

	private static String addrHex(Address a) {
		return a.toString().replace(":", "_");
	}

	// -------------------------------------------------------------------- SUMMARY.md

	private static final String[][] SUBSYSTEM_SEARCHES = {
		{ "filesystem_init", "---- Initializing file system ----", "F_Init" },
		{ "object_loader", "G_LoadObjects" },
		{ "model_loader", "R_LoadModel" },
		{ "level_list_loader", "G_LoadLevelList" },
		{ "level_heightmap_loader", "HMAP" },
		{ "script_loader", "RCSL" },
		{ "script_interpreter_loop", "Thread stack overflow", "Script stall detected." },
		{ "builtin_lookup", "SL_GetExternFunc" },
		{ "save_file_loader", "G_LoadBin" },
		{ "sound_init", "S_Init" },
		{ "opengl_init", "GL_Init" },
	};

	// Subsystems identified by a raw 4-byte magic tag compared in code, rather than a
	// printed string, so the SUBSYSTEM_SEARCHES string search above can never find them.
	private static final Map<String, String> MAGIC_FALLBACK = Map.of(
		"level_heightmap_loader", "HMAP",
		"script_loader", "RCSL");

	/** Finds the first occurrence of the literal ASCII bytes of {@code tag} anywhere in
	 * .text (e.g. a 4-character file-format magic number compared as an immediate/constant),
	 * returning its address, or null if not found. */
	private Address findAsciiConstantInText(String tag) {
		MemoryBlock textBlock = currentProgram.getMemory().getBlock(".text");
		if (textBlock == null) return null;
		byte[] pattern = tag.getBytes(StandardCharsets.US_ASCII);
		try {
			return currentProgram.getMemory().findBytes(textBlock.getStart(), textBlock.getEnd(),
				pattern, null, true, monitor);
		}
		catch (Exception ex) {
			return null;
		}
	}

	private void writeSummary() throws IOException {
		File f = new File(outDir, "SUMMARY.md");
		try (PrintWriter pw = new PrintWriter(new FileWriter(f))) {
			pw.println("# " + probeTitle + " Ghidra export summary");
			pw.println();
			pw.println("Generated by re/ghidra_scripts/ExportAll.java. Gitignored (re/out/), for the");
			pw.println("orchestrator and later agents; not a spec document.");
			pw.println();
			pw.println("- Total functions: " + allFuncs.size());
			pw.println("- Decompiled OK: " + decompOk);
			pw.println("- Decompile failed/timeout: " + decompFailed);
			pw.println("- Total defined strings: " + stringText.size());
			pw.println();

			pw.println("## Entry point / WinMain");
			pw.println();
			writeEntryAndWinMain(pw);
			pw.println();

			pw.println("## Subsystem functions (identified via referenced strings)");
			pw.println();
			for (String[] spec : SUBSYSTEM_SEARCHES) {
				String label = spec[0];
				Set<Address> funcs = new TreeSet<>();
				List<String> matchedStrings = new ArrayList<>();
				for (int i = 1; i < spec.length; i++) {
					String needle = spec[i];
					for (Map.Entry<Address, String> e : stringText.entrySet()) {
						if (e.getValue().contains(needle)) {
							matchedStrings.add(e.getValue());
							funcs.addAll(stringRefs.getOrDefault(e.getKey(), Collections.emptySet()));
						}
					}
				}
				pw.println("### " + label);
				if (funcs.isEmpty() && MAGIC_FALLBACK.containsKey(label)) {
					// Some formats are identified by a 4-byte magic tag compared as an
					// immediate/constant in code (e.g. "HMAP", "RCSL"), not printed anywhere,
					// so no string search can find them; fall back to a raw byte scan of
					// .text for the literal ASCII tag.
					String magic = MAGIC_FALLBACK.get(label);
					Address hit = findAsciiConstantInText(magic);
					if (hit != null) {
						Function fn = fm.getFunctionContaining(hit);
						if (fn != null) {
							pw.println("- **" + addrStr(fn.getEntryPoint()) + "** `" + fn.getName() +
								"` size=" + fn.getBody().getNumAddresses() +
								" (found via raw \"" + magic + "\" magic-constant scan at " +
								addrStr(hit) + " -- not a printed string, so not found by the " +
								"string-reference search above)");
						}
						else {
							pw.println("- \"" + magic + "\" magic constant found at " + addrStr(hit) +
								" but it is not inside any known function");
						}
					}
					else {
						pw.println("- not found (no matching string, no referencing function, " +
							"and no raw \"" + magic + "\" byte sequence in .text)");
					}
				}
				else if (funcs.isEmpty()) {
					pw.println("- not found (no matching string / no referencing function)");
				}
				else if (funcs.size() == 1) {
					Address a = funcs.iterator().next();
					Function fn = fm.getFunctionAt(a);
					pw.println("- **" + addrStr(a) + "** `" + (fn == null ? "?" : fn.getName()) +
						"` size=" + (fn == null ? "?" : fn.getBody().getNumAddresses()));
				}
				else {
					pw.println("- ambiguous, " + funcs.size() + " candidate functions:");
					for (Address a : funcs) {
						Function fn = fm.getFunctionAt(a);
						pw.println("  - " + addrStr(a) + " `" + (fn == null ? "?" : fn.getName()) + "`");
					}
				}
				if (!matchedStrings.isEmpty()) {
					pw.println("  - matched strings: " +
						String.join(" | ", new LinkedHashSet<>(matchedStrings)));
				}
				pw.println();
			}

			pw.println("## Acceptance-check strings");
			pw.println();
			String[] checks = ACCEPTANCE_STRINGS;
			for (String needle : checks) {
				pw.println("- `" + needle + "`:");
				boolean found = false;
				String needleTrimmed = needle.replaceAll("[\\r\\n]+$", "");
				for (Map.Entry<Address, String> e : stringText.entrySet()) {
					String textTrimmed = e.getValue().replaceAll("[\\r\\n]+$", "");
					if (textTrimmed.equals(needleTrimmed)) {
						found = true;
						Set<Address> funcs = stringRefs.getOrDefault(e.getKey(), Collections.emptySet());
						pw.println("  - string at " + addrStr(e.getKey()) + ", referenced by: " +
							(funcs.isEmpty() ? "(none found)" : addrListStr(funcs)));
					}
				}
				if (!found) {
					pw.println("  - NOT FOUND as an exact string match");
				}
			}
			pw.println();

			pw.println("## 30 largest functions");
			pw.println();
			List<Function> byName = new ArrayList<>(allFuncs.values());
			byName.sort((a, b) -> Long.compare(b.getBody().getNumAddresses(), a.getBody().getNumAddresses()));
			int n = Math.min(30, byName.size());
			for (int i = 0; i < n; i++) {
				Function fn = byName.get(i);
				List<Address> strs = funcStrings.getOrDefault(fn.getEntryPoint(), Collections.emptyList());
				List<String> texts = new ArrayList<>();
				int shown = 0;
				for (Address sa : strs) {
					texts.add(stringText.get(sa));
					shown++;
					if (shown >= 6) break;
				}
				pw.println((i + 1) + ". " + addrStr(fn.getEntryPoint()) + " `" + fn.getName() +
					"` size=" + fn.getBody().getNumAddresses() +
					(texts.isEmpty() ? "" : " strings=" + texts));
			}
			pw.println();
			writeProbeResults(pw);
		}
	}

	/** Probe results: the probe lists (re/probes/<tag>.json) checked against the tables the
	 * scan found, plus the imports per DLL. */
	private void writeProbeResults(PrintWriter pw) {
		pw.println("## Probe results (" + probeTag + ")");
		pw.println();
		pw.println("- image base: " + addrStr(currentProgram.getImageBase()) +
			", executable format: " + currentProgram.getExecutableFormat());
		probeTable(pw, "builtin", "builtin_function_table", 8, BUILTIN_NAMES,
			"builtin_table", "builtin_count");
		probeTable(pw, "global", "global_variable_name_table", 12, GLOBAL_NAMES,
			"global_table", "global_count");
		pw.println();
		pw.println("### Imports");
		pw.println();
		Map<String, Integer> perDll = new TreeMap<>();
		FunctionIterator extIt = fm.getExternalFunctions();
		int total = 0;
		while (extIt.hasNext()) {
			Function ef = extIt.next();
			String dll = "UNKNOWN";
			try {
				if (ef.getExternalLocation() != null &&
					ef.getExternalLocation().getLibraryName() != null) {
					dll = ef.getExternalLocation().getLibraryName();
				}
			}
			catch (Exception ex) {
				// UNKNOWN
			}
			perDll.merge(dll, 1, Integer::sum);
			total++;
		}
		pw.println("- total imported functions: " + total);
		for (Map.Entry<String, Integer> e : perDll.entrySet()) {
			pw.println("- " + e.getKey() + ": " + e.getValue());
		}
	}

	private void probeTable(PrintWriter pw, String label, String kind, int stride,
		String[] names, String expAddrKey, String expCountKey) {
		Set<String> probe = new LinkedHashSet<>(Arrays.asList(names));
		JsonObject best = null;
		int bestMatches = -1;
		for (int i = 0; i < foundTables.size(); i++) {
			JsonObject t = foundTables.get(i).getAsJsonObject();
			if (t.get("entry_stride").getAsInt() != stride) continue;
			int m = 0;
			for (var el : t.getAsJsonArray("entries")) {
				JsonElement st = el.getAsJsonObject().get("string_text");
				if (st != null && !st.isJsonNull() && probe.contains(st.getAsString())) m++;
			}
			if (m > bestMatches) {
				bestMatches = m;
				best = t;
			}
		}
		pw.println();
		pw.println("### " + label + " table (stride " + stride + ")");
		pw.println();
		String expAddr = probeExpected.has(expAddrKey) ? probeExpected.get(expAddrKey).getAsString() : "?";
		int expCount = probeExpected.has(expCountKey) ? probeExpected.get(expCountKey).getAsInt() : -1;
		if (best == null) {
			pw.println("- NO table with stride " + stride + " found");
			return;
		}
		Set<String> seen = new HashSet<>();
		String firstMatchAddr = null;
		int firstMatchIdx = -1, lastMatchIdx = -1, idx = 0, nonMatching = 0;
		List<String> nonMatchingNames = new ArrayList<>();
		for (var el : best.getAsJsonArray("entries")) {
			JsonObject e = el.getAsJsonObject();
			JsonElement st = e.get("string_text");
			String txt = (st == null || st.isJsonNull()) ? null : st.getAsString();
			if (txt != null && probe.contains(txt)) {
				seen.add(txt);
				if (firstMatchIdx < 0) {
					firstMatchIdx = idx;
					firstMatchAddr = e.get("record_address").getAsString();
				}
				lastMatchIdx = idx;
			}
			else {
				nonMatching++;
				nonMatchingNames.add(txt);
			}
			idx++;
		}
		int span = firstMatchIdx < 0 ? 0 : lastMatchIdx - firstMatchIdx + 1;
		List<String> missing = new ArrayList<>();
		for (String n : probe) if (!seen.contains(n)) missing.add(n);
		pw.println("- detected run: " + best.get("address").getAsString() + ", " +
			best.get("entry_count").getAsInt() + " records, kind_guess=" +
			best.get("kind_guess").getAsString() + " (expected kind " + kind + ")");
		pw.println("- first record matching a probe name: " + firstMatchAddr +
			" (run start plus " + Math.max(firstMatchIdx, 0) + " records); matching span " + span +
			" records, of which " + seen.size() + " are probe names");
		pw.println("- records in the run that are not probe names: " + nonMatching + " " +
			nonMatchingNames);
		pw.println("- probe names found: " + seen.size() + " of " + probe.size());
		pw.println("- probe names MISSING: " + (missing.isEmpty() ? "none" : missing));
		pw.println("- expected (probe file): table at " + expAddr + ", " + expCount + " entries; " +
			"table address " + (expAddr.equalsIgnoreCase(firstMatchAddr) ||
				expAddr.equalsIgnoreCase(best.get("address").getAsString()) ? "CONFIRMED" :
				"DIFFERS (found " + firstMatchAddr + ")") + ", entry count " +
			(span == expCount ? "CONFIRMED" : "DIFFERS (matching span " + span + ")"));
	}

	private void writeEntryAndWinMain(PrintWriter pw) {
		Address entryAddr = null;
		SymbolTable st = currentProgram.getSymbolTable();
		Iterator<Address> epIt = st.getExternalEntryPointIterator();
		if (epIt.hasNext()) {
			entryAddr = epIt.next();
		}
		if (entryAddr == null) {
			Symbol s = null;
			for (Symbol sym : st.getSymbols("entry")) { s = sym; break; }
			if (s != null) entryAddr = s.getAddress();
		}
		pw.println("- entry point: " + (entryAddr == null ? "NOT FOUND" : addrStr(entryAddr)));

		if (entryAddr == null) {
			pw.println("- WinMain: NOT FOUND (no entry point)");
			return;
		}
		Function entryFunc = fm.getFunctionAt(entryAddr);
		if (entryFunc == null) {
			pw.println("- WinMain: NOT FOUND (entry point has no function)");
			return;
		}

		// BFS from entry through direct callees, depth-limited, scoring by how many
		// well-known USER32 windowing APIs each candidate calls.
		Set<String> winapiHints = new HashSet<>(Arrays.asList("RegisterClassA", "RegisterClassExA",
			"RegisterClassW", "RegisterClassExW", "CreateWindowExA", "CreateWindowExW",
			"CreateWindowA", "CreateWindowW", "ShowWindow", "UpdateWindow", "GetMessageA",
			"GetMessageW", "TranslateMessage", "DispatchMessageA", "DispatchMessageW",
			"PeekMessageA", "PeekMessageW"));

		Deque<Function> queue = new ArrayDeque<>();
		Set<Address> visited = new HashSet<>();
		queue.add(entryFunc);
		visited.add(entryFunc.getEntryPoint());
		Function best = null;
		int bestScore = 0;
		int depth = 0;
		List<Function> frontier = new ArrayList<>(queue);
		while (!frontier.isEmpty() && depth < 6) {
			List<Function> next = new ArrayList<>();
			for (Function cur : frontier) {
				if (monitor.isCancelled()) break;
				Set<Function> callees;
				try {
					callees = cur.getCalledFunctions(monitor);
				}
				catch (Exception ex) {
					continue;
				}
				int score = 0;
				for (Function c : callees) {
					if (c.isExternal() && winapiHints.contains(c.getName())) score++;
				}
				if (!cur.isExternal() && score > bestScore) {
					bestScore = score;
					best = cur;
				}
				for (Function c : callees) {
					if (!c.isExternal() && visited.add(c.getEntryPoint())) {
						next.add(c);
					}
				}
			}
			frontier = next;
			depth++;
		}

		if (best != null && bestScore >= 2) {
			pw.println("- WinMain (heuristic: calls " + bestScore +
				" of the known USER32 window-loop APIs, found via BFS from entry): " +
				addrStr(best.getEntryPoint()) + " `" + best.getName() + "` size=" +
				best.getBody().getNumAddresses());
		}
		else {
			pw.println("- WinMain: NOT CONFIDENTLY IDENTIFIED (best candidate " +
				(best == null ? "none" : addrStr(best.getEntryPoint()) + " score=" + bestScore) +
				")");
		}
	}

	private String addrListStr(Collection<Address> addrs) {
		List<String> l = new ArrayList<>();
		for (Address a : addrs) l.add(addrStr(a));
		return String.join(", ", l);
	}

	private static String addrStr(Address a) {
		return "0x" + a.toString(false, false);
	}
}
