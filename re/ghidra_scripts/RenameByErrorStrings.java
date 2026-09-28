// Naming pass: renames functions that uniquely reference a Quake-style error/log string
// ("Identifier: message" or "Identifier (message"), using the identifier as the new name.
// Only renames when exactly one function in the program references any string sharing that
// identifier; otherwise the default name is left untouched and the candidate is recorded
// with confidence "ambiguous".
//
// Writes a CSV mapping (address,name,evidence_string,confidence) to the path given as the
// first script argument. Rows with confidence "renamed" reflect an actual rename applied to
// the program. Rows with confidence "ambiguous" are candidates only, for a human/later agent
// to disambiguate; the function's address in that row is one of the (multiple) candidates,
// not a function that was renamed.
//
// Usage (headless): -postScript RenameByErrorStrings.java <symbols_csv_path>
//@category AirStrike3D

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

public class RenameByErrorStrings extends GhidraScript {

	// Identifier followed by ": " or " (" e.g. "R_LoadModel: Couldn't open ..." or
	// "G_LoadObjects (foo)".
	private static final Pattern ID_PATTERN =
		Pattern.compile("^([A-Za-z_][A-Za-z0-9_]*)\\s*(?::|\\()");

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 1) {
			printerr("RenameByErrorStrings: expected <symbols_csv_path> argument");
			return;
		}
		File csvFile = new File(args[0]);
		File parent = csvFile.getParentFile();
		if (parent != null) {
			parent.mkdirs();
		}

		FunctionManager fm = currentProgram.getFunctionManager();
		Listing listing = currentProgram.getListing();

		// identifier -> set of function entry addresses referencing any string with that id
		Map<String, Set<Address>> idToFuncs = new TreeMap<>();
		// identifier -> one example string text (first found)
		Map<String, String> idToExample = new HashMap<>();

		DataIterator dataIt = listing.getDefinedData(true);
		int stringCount = 0;
		while (dataIt.hasNext()) {
			if (monitor.isCancelled()) {
				break;
			}
			Data data = dataIt.next();
			if (!StringDataInstance.isString(data)) {
				continue;
			}
			StringDataInstance sdi = StringDataInstance.getStringDataInstance(data);
			String text = sdi.getStringValue();
			if (text == null || text.isEmpty()) {
				continue;
			}
			Matcher m = ID_PATTERN.matcher(text);
			if (!m.find()) {
				continue;
			}
			String id = m.group(1);
			// Skip very short/generic identifiers (likely false positives, e.g. single
			// letters or common words unrelated to the Quake-style subsystem prefixes).
			if (id.length() < 2) {
				continue;
			}
			stringCount++;

			Address strAddr = data.getAddress();
			Reference[] refs = getReferencesTo(strAddr);
			Set<Address> funcsForThisString = new TreeSet<>();
			for (Reference ref : refs) {
				Function f = fm.getFunctionContaining(ref.getFromAddress());
				if (f != null) {
					funcsForThisString.add(f.getEntryPoint());
				}
			}
			if (funcsForThisString.isEmpty()) {
				continue;
			}
			idToFuncs.computeIfAbsent(id, k -> new TreeSet<>()).addAll(funcsForThisString);
			idToExample.putIfAbsent(id, text);
		}

		println("RenameByErrorStrings: scanned " + stringCount +
			" candidate error/log strings, " + idToFuncs.size() + " distinct identifiers");

		List<String[]> rows = new ArrayList<>(); // address,name,evidence,confidence
		int renamed = 0;
		int ambiguous = 0;
		// A single function can uniquely match more than one identifier (e.g. a generic
		// settings-label handler that prints both "Brightness:" and "Camera:"). Only the
		// first identifier processed (alphabetical order, since idToFuncs is a TreeMap) is
		// actually applied; later ones for the same function are recorded for visibility but
		// must not silently overwrite the applied name (which would leave a stale "renamed"
		// row in the CSV for the name that's no longer current).
		Map<Address, String> appliedNameByFunc = new HashMap<>();

		int tx = currentProgram.startTransaction("RenameByErrorStrings");
		boolean success = false;
		try {
			for (Map.Entry<String, Set<Address>> e : idToFuncs.entrySet()) {
				String id = e.getKey();
				Set<Address> funcs = e.getValue();
				String evidence = idToExample.get(id);

				if (funcs.size() == 1) {
					Address entry = funcs.iterator().next();
					Function f = fm.getFunctionAt(entry);
					if (f == null) {
						continue;
					}
					String already = appliedNameByFunc.get(entry);
					if (already != null) {
						rows.add(new String[] { entry.toString(), id, evidence,
							"also_matches(function already renamed to '" + already + "')" });
						continue;
					}
					String newName = id;
					String appliedName = tryRename(f, newName);
					appliedNameByFunc.put(entry, appliedName);
					rows.add(new String[] { entry.toString(), appliedName, evidence, "renamed" });
					renamed++;
				}
				else {
					for (Address a : funcs) {
						rows.add(new String[] { a.toString(), id, evidence,
							"ambiguous(" + funcs.size() + " candidates)" });
						ambiguous++;
					}
				}
			}
			success = true;
		}
		finally {
			currentProgram.endTransaction(tx, success);
		}

		try (PrintWriter pw = new PrintWriter(new FileWriter(csvFile))) {
			pw.println("address,name,evidence_string,confidence");
			for (String[] row : rows) {
				pw.println(csv(row[0]) + "," + csv(row[1]) + "," + csv(row[2]) + "," +
					csv(row[3]));
			}
		}

		println("RenameByErrorStrings: renamed " + renamed + " functions, " + ambiguous +
			" ambiguous candidate rows written to " + csvFile.getAbsolutePath());
	}

	/** Renames f to baseName, appending _2, _3, ... on collision. Returns the applied name. */
	private String tryRename(Function f, String baseName) {
		String candidate = baseName;
		int suffix = 2;
		while (true) {
			try {
				f.setName(candidate, SourceType.USER_DEFINED);
				return candidate;
			}
			catch (Exception ex) {
				candidate = baseName + "_" + suffix;
				suffix++;
				if (suffix > 50) {
					// give up, leave default name
					return f.getName();
				}
			}
		}
	}

	private static String csv(String s) {
		if (s == null) {
			s = "";
		}
		if (s.contains(",") || s.contains("\"") || s.contains("\n")) {
			s = "\"" + s.replace("\"", "\"\"") + "\"";
		}
		return s;
	}
}
