// Tests for the generic brace-block text parser. See docs/spec/text-blocks.md.
#include "doctest.h"

#include "as3d/textblock.h"
#include "test_data.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace as3d;

namespace {

TextFile parseStr(const std::string& s) {
    return parseTextBlocks(reinterpret_cast<const u8*>(s.data()), s.size());
}

struct Counts {
    int blocks = 0;
    int statements = 0;
    int tokens = 0;
};

Counts computeCounts(const TextFile& tf) {
    Counts c;
    c.blocks = static_cast<int>(tf.blocks.size());
    for (const auto& b : tf.blocks) {
        if (!b.name.empty()) c.tokens++;
        c.statements += static_cast<int>(b.statements.size());
        for (const auto& s : b.statements) {
            c.tokens += 1 + static_cast<int>(s.args.size());
        }
    }
    return c;
}

std::string slurpFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Minimal, purpose-built reader for testdata/golden/<game>/textblock_counts.json:
// the file is produced by tools/ref/test_textblock.py with a known, simple
// shape, so a couple of regexes are enough without pulling in a JSON lib.
std::map<std::string, Counts> loadGoldenCounts(const std::string& jsonText) {
    std::map<std::string, Counts> out;
    static const std::regex re(
        R"re("([^"]+)"\s*:\s*\{\s*"blocks"\s*:\s*(\d+)\s*,\s*"statements"\s*:\s*(\d+)\s*,\s*"tokens"\s*:\s*(\d+)\s*\})re");
    for (auto it = std::sregex_iterator(jsonText.begin(), jsonText.end(), re);
         it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        Counts c;
        c.blocks = std::stoi(m[2]);
        c.statements = std::stoi(m[3]);
        c.tokens = std::stoi(m[4]);
        out[m[1]] = c;
    }
    return out;
}

int extractIntField(const std::string& jsonText, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*(\\d+)");
    std::smatch m;
    if (std::regex_search(jsonText, m, re)) return std::stoi(m[1]);
    return -1;
}

// The files that use this syntax (see docs/spec/text-blocks.md: maps/levels.txt and every
// .obj, .wpn and .ps file; 42 in the first game) are the keys of the golden counts, as game
// paths for testdata::readExtracted. Their number is checked against expected.json.
std::vector<std::string> allTextBlockFiles(const std::map<std::string, Counts>& golden) {
    std::vector<std::string> files;
    for (const auto& kv : golden) {
        std::string p = kv.first;
        for (char& c : p) {
            if (c == '/') c = '\\';
        }
        files.push_back(p);
    }
    return files;
}

// Same normalization tools/ref/textblock.py uses for the golden JSON keys:
// forward slashes, as under assets_extracted/.
std::string toGoldenKey(const std::string& gamePath) {
    std::string out = gamePath;
    for (char& c : out) {
        if (c == '\\') c = '/';
    }
    return out;
}

} // namespace

TEST_CASE("all game text files parse with zero errors and match the golden counts") {
    AS3D_REQUIRE_DATA();

    // The golden file is checked into *this* repository (worktree), unlike the gitignored game
    // data; goldenDir() is relative to AS3D_REPO_ROOT.
    std::string goldenText = slurpFile(testdata::goldenDir() + "/textblock_counts.json");
    REQUIRE(!goldenText.empty());
    std::map<std::string, Counts> golden = loadGoldenCounts(goldenText);
    REQUIRE(golden.size() == static_cast<size_t>(testdata::expectedInt("text_blocks.files")));

    int totalNamedObjectBlocks = 0;
    std::map<std::string, int> objectNameCounts;

    for (const auto& gamePath : allTextBlockFiles(golden)) {
        Blob blob;
        REQUIRE(testdata::readExtracted(gamePath, blob));
        TextFile tf = parseTextBlocks(blob.data(), blob.size());
        INFO("file: ", gamePath);
        CHECK(tf.errors.empty());

        std::string key = toGoldenKey(gamePath);
        auto it = golden.find(key);
        REQUIRE(it != golden.end());
        Counts actual = computeCounts(tf);
        CHECK(actual.blocks == it->second.blocks);
        CHECK(actual.statements == it->second.statements);
        CHECK(actual.tokens == it->second.tokens);

        if (gamePath.rfind("objects\\", 0) == 0) {
            for (const auto& b : tf.blocks) {
                totalNamedObjectBlocks++;
                objectNameCounts[b.name]++;
            }
        }
    }

    int distinctObjectNames = static_cast<int>(objectNameCounts.size());
    int expectedTotal = extractIntField(goldenText, "objects_total_named_blocks");
    int expectedDistinct = extractIntField(goldenText, "objects_distinct_names");
    CHECK(totalNamedObjectBlocks == expectedTotal);
    CHECK(distinctObjectNames == expectedDistinct);

    // VERIFIED-DATA (see docs/spec/text-blocks.md): the first game's 864 blocks (863 names) match
    // its "G_LoadObjects: 864 objects parsed succefully" log line exactly.
    CHECK(totalNamedObjectBlocks == testdata::expectedInt("text_blocks.object_blocks"));
    CHECK(distinctObjectNames == testdata::expectedInt("text_blocks.distinct_object_names"));
}

TEST_CASE("TextToken number classification") {
    TextToken bare;
    bare.text = "380";
    bare.quoted = false;
    CHECK(bare.isNumber());
    CHECK(bare.asInt() == 380);
    CHECK(bare.asFloat() == doctest::Approx(380.0f));

    TextToken neg;
    neg.text = "-120";
    CHECK(neg.isNumber());
    CHECK(neg.asInt() == -120);

    TextToken dec;
    dec.text = "-1.0";
    CHECK(dec.isNumber());
    CHECK(dec.asFloat() == doctest::Approx(-1.0f));

    TextToken frac;
    frac.text = "0.4";
    CHECK(frac.isNumber());
    CHECK(frac.asFloat() == doctest::Approx(0.4f));

    TextToken quotedNumber;
    quotedNumber.text = "380";
    quotedNumber.quoted = true;
    CHECK_FALSE(quotedNumber.isNumber());
    CHECK(quotedNumber.asFloat() == 0.0f);
    CHECK(quotedNumber.asInt() == 0);

    TextToken word;
    word.text = "FL_ONGROUND_NORMAL";
    CHECK_FALSE(word.isNumber());
    CHECK(word.asFloat() == 0.0f);
    CHECK(word.asInt() == 0);

    TextToken bareDot;
    bareDot.text = ".5";
    CHECK_FALSE(bareDot.isNumber());

    TextToken trailingDot;
    trailingDot.text = "5.";
    CHECK_FALSE(trailingDot.isNumber());

    TextToken justMinus;
    justMinus.text = "-";
    CHECK_FALSE(justMinus.isNumber());

    TextToken empty;
    empty.text = "";
    CHECK_FALSE(empty.isNumber());
}

TEST_CASE("TextBlock::find is case-insensitive and returns the first match") {
    std::string src =
        "tank_small_green {\n"
        "\thealth\t380\n"
        "\tHEALTH\t999\n"
        "\tenemy\n"
        "}\n";
    TextFile tf = parseStr(src);
    REQUIRE(tf.blocks.size() == 1);
    const TextBlock& b = tf.blocks[0];
    CHECK(b.name == "tank_small_green");
    const TextStatement* health = b.find("Health");
    REQUIRE(health != nullptr);
    CHECK(health->args.size() == 1);
    CHECK(health->args[0].asInt() == 380); // first match wins, not the later duplicate
    CHECK(b.find("enemy") != nullptr);
    CHECK(b.find("nope") == nullptr);
}

TEST_CASE("basic named block with typed arguments") {
    std::string src =
        "// a leading comment\n"
        "tank_small_green {\n"
        "\thealth\t380\n"
        "\tflag    FL_ONGROUND_NORMAL\n"
        "\tenemy\n"
        "\tmodel\t\"models/tanks/tank_small/tank1.mdl\"\n"
        "\tattach abs id \"GUNS\"  \"tank_small_green_guns\"  \"tag_guns\"\n"
        "}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 1);
    const TextBlock& b = tf.blocks[0];
    CHECK(b.name == "tank_small_green");
    REQUIRE(b.statements.size() == 5);
    CHECK(b.statements[0].key == "health");
    CHECK(b.statements[0].args.size() == 1);
    CHECK(b.statements[2].key == "enemy");
    CHECK(b.statements[2].args.empty());
    const TextStatement& attach = b.statements[4];
    CHECK(attach.key == "attach");
    REQUIRE(attach.args.size() == 5);
    CHECK_FALSE(attach.args[0].quoted); // abs
    CHECK_FALSE(attach.args[1].quoted); // id
    CHECK(attach.args[2].quoted);       // "GUNS"
    CHECK(attach.args[2].text == "GUNS");
}

TEST_CASE("anonymous blocks, as in levels.txt") {
    std::string src =
        "{\n"
        "\tname\t\t\"Mission 1: Tutorial\"\n"
        "\tfog \t\t0.4 0.4 0.3   650 900\n"
        "\tnight\n"
        "}\n"
        "\n"
        "{\n"
        "\tid\t\t\"mission2\"\n"
        "}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 2);
    CHECK(tf.blocks[0].name.empty());
    CHECK(tf.blocks[1].name.empty());
    const TextStatement* fog = tf.blocks[0].find("fog");
    REQUIRE(fog != nullptr);
    REQUIRE(fog->args.size() == 5);
    CHECK(fog->args[3].asInt() == 650);
}

TEST_CASE("block name and '{' on separate lines") {
    // As seen verbatim in objects/boss1.obj line 27-28.
    std::string src =
        "boss1_rocketlaucher\n"
        "{\n"
        "\thealth\t800\n"
        "}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 1);
    CHECK(tf.blocks[0].name == "boss1_rocketlaucher");
    CHECK(tf.blocks[0].statements.size() == 1);
}

TEST_CASE("duplicate block names are both kept") {
    std::string src =
        "tank_dead {\n"
        "\tmodel \"a.mdl\"\n"
        "}\n"
        "tank_dead {\n"
        "\tmodel \"b.mdl\"\n"
        "}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 2);
    CHECK(tf.blocks[0].name == "tank_dead");
    CHECK(tf.blocks[1].name == "tank_dead");
    CHECK(tf.blocks[0].find("model")->args[0].text == "a.mdl");
    CHECK(tf.blocks[1].find("model")->args[0].text == "b.mdl");
}

TEST_CASE("line comments end a statement, with or without preceding whitespace") {
    // Real data always has whitespace (or line start) before `//`; the
    // parser also recognizes it defensively when glued to a token, since
    // nothing in the grammar requires a word boundary there.
    std::string src =
        "block {\n"
        "\t// full-line comment\n"
        "\thealth 100 // trailing comment\n"
        "\tscore 5//glued comment, no space\n"
        "}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 1);
    REQUIRE(tf.blocks[0].statements.size() == 2);
    CHECK(tf.blocks[0].statements[0].args.size() == 1);
    CHECK(tf.blocks[0].statements[0].args[0].asInt() == 100);
    REQUIRE(tf.blocks[0].statements[1].args.size() == 1);
    CHECK(tf.blocks[0].statements[1].args[0].asInt() == 5);
}

TEST_CASE("CRLF and bare LF parse identically") {
    std::string crlf = "block {\r\n\thealth\t380\r\n}\r\n";
    std::string lf = "block {\n\thealth\t380\n}\n";
    TextFile a = parseStr(crlf);
    TextFile b = parseStr(lf);
    CHECK(a.errors.empty());
    CHECK(b.errors.empty());
    REQUIRE(a.blocks.size() == 1);
    REQUIRE(b.blocks.size() == 1);
    CHECK(computeCounts(a).tokens == computeCounts(b).tokens);
    CHECK(a.blocks[0].statements[0].args[0].asInt() == 380);
    CHECK(b.blocks[0].statements[0].args[0].asInt() == 380);
}

TEST_CASE("tabs and spaces are interchangeable whitespace") {
    std::string src = "block {\n\twater\t \t\"gfx\\\\water\\\\lake.tga\" -68 0.4\n}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 1);
    const TextStatement* water = tf.blocks[0].find("water");
    REQUIRE(water != nullptr);
    REQUIRE(water->args.size() == 3);
    CHECK(water->args[1].asInt() == -68);
}

TEST_CASE("empty input produces an empty TextFile") {
    TextFile tf = parseTextBlocks(nullptr, 0);
    CHECK(tf.blocks.empty());
    CHECK(tf.errors.empty());
}

TEST_CASE("unterminated quoted string is recorded as an error and recovered from") {
    std::string src =
        "block {\n"
        "\tmodel \"models/x.mdl\n"
        "\thealth 10\n"
        "}\n";
    TextFile tf = parseStr(src);
    REQUIRE(tf.errors.size() == 1);
    CHECK(tf.errors[0].find("line 2") != std::string::npos);
    CHECK(tf.errors[0].find("unterminated string") != std::string::npos);
    REQUIRE(tf.blocks.size() == 1);
    REQUIRE(tf.blocks[0].statements.size() == 2);
    // The rest of the line became the string's content.
    CHECK(tf.blocks[0].statements[0].args[0].text == "models/x.mdl");
    // Parsing recovered and continued past the bad line.
    CHECK(tf.blocks[0].statements[1].key == "health");
}

TEST_CASE("stray '}' with no open block is an error and is skipped") {
    std::string src = "}\nblock {\n\thealth 10\n}\n";
    TextFile tf = parseStr(src);
    REQUIRE(tf.errors.size() == 1);
    CHECK(tf.errors[0].find("line 1") != std::string::npos);
    CHECK(tf.errors[0].find("unexpected '}'") != std::string::npos);
    REQUIRE(tf.blocks.size() == 1);
    CHECK(tf.blocks[0].name == "block");
}

TEST_CASE("a block name never followed by '{' is an error, and parsing resyncs") {
    std::string src = "stray_name\nblock {\n\thealth 10\n}\n";
    TextFile tf = parseStr(src);
    REQUIRE(tf.errors.size() == 1);
    CHECK(tf.errors[0].find("expected '{'") != std::string::npos);
    CHECK(tf.errors[0].find("stray_name") != std::string::npos);
    REQUIRE(tf.blocks.size() == 1);
    CHECK(tf.blocks[0].name == "block");
}

TEST_CASE("a block name pending at end of file is an error") {
    std::string src = "block {\n\thealth 10\n}\ntrailing_name\n";
    TextFile tf = parseStr(src);
    REQUIRE(tf.errors.size() == 1);
    CHECK(tf.errors[0].find("end of file") != std::string::npos);
    REQUIRE(tf.blocks.size() == 1);
}

TEST_CASE("an unclosed block at end of file is an error but is still returned") {
    std::string src = "block {\n\thealth 10\n";
    TextFile tf = parseStr(src);
    REQUIRE(tf.errors.size() == 1);
    CHECK(tf.errors[0].find("unterminated block") != std::string::npos);
    REQUIRE(tf.blocks.size() == 1);
    CHECK(tf.blocks[0].name == "block");
    CHECK(tf.blocks[0].statements.size() == 1);
}

TEST_CASE("a nested '{' is flattened into the parent block with an error") {
    std::string src =
        "outer {\n"
        "\thealth 10\n"
        "\tinner_name\n"
        "\t{\n"
        "\t\tskin \"x.tga\"\n"
        "\t}\n"
        "\tscore 5\n"
        "}\n";
    TextFile tf = parseStr(src);
    REQUIRE(tf.errors.size() == 1);
    CHECK(tf.errors[0].find("nested '{'") != std::string::npos);
    CHECK(tf.errors[0].find("outer") != std::string::npos);
    REQUIRE(tf.blocks.size() == 1);
    const TextBlock& b = tf.blocks[0];
    CHECK(b.name == "outer");
    // Flattened: the outer block still closes correctly, all statements
    // (including the inner block's own name and its statements) end up on it.
    CHECK(b.find("health") != nullptr);
    CHECK(b.find("score") != nullptr);
    CHECK(b.find("skin") != nullptr);
    CHECK(b.find("inner_name") != nullptr); // degenerate zero-arg statement
}

TEST_CASE("bytes above 0x7F are passed through untouched, in comments and strings") {
    std::string src =
        "block { // \xC7\xE4\xF0\xE0\xE2\xF1\xF2\xE2\xF3\xE9\xF2\xE5\n"
        "\tskin \"\xC7\xE4\xF0\xE0\xE2\xF1\xF2\xE2\xF3\xE9\xF2\xE5.tga\"\n"
        "}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 1);
    const TextStatement* skin = tf.blocks[0].find("skin");
    REQUIRE(skin != nullptr);
    CHECK(skin->args[0].text == "\xC7\xE4\xF0\xE0\xE2\xF1\xF2\xE2\xF3\xE9\xF2\xE5.tga");
}

TEST_CASE("embedded NUL bytes do not truncate parsing") {
    // The quoted string's content is "a\0b.mdl" (7 bytes): a NUL byte must
    // not act like a C-string terminator anywhere in the parser.
    std::string inner = std::string("a") + '\0' + "b.mdl";
    REQUIRE(inner.size() == 7);
    std::string src = "block {\n\tmodel \"" + inner + "\"\n}\n";
    TextFile tf = parseStr(src);
    CHECK(tf.errors.empty());
    REQUIRE(tf.blocks.size() == 1);
    const TextStatement* model = tf.blocks[0].find("model");
    REQUIRE(model != nullptr);
    CHECK(model->args[0].text == inner);
    CHECK(model->args[0].text.size() == 7);
}

TEST_CASE("a file that is nothing but stray junk still terminates cleanly") {
    std::string src = "} } } foo bar \"unterminated\nbaz {\n}\n} { {\n";
    TextFile tf = parseStr(src);
    // Must not crash or hang; some errors are expected, and any well-formed
    // blocks found along the way are still returned.
    CHECK(tf.errors.size() > 0);
}

TEST_CASE("fuzz: mutated real file and random bytes never crash or hang") {
    AS3D_REQUIRE_DATA();
    Blob seed;
    // tanks.obj where the game has it, else its first object file.
    std::string seedFile = "objects\\tanks.obj";
    if (!testdata::readExtracted(seedFile, seed)) {
        for (const auto& f : allTextBlockFiles(loadGoldenCounts(slurpFile(testdata::goldenDir() + "/textblock_counts.json")))) {
            if (f.rfind("objects\\", 0) == 0) { seedFile = f; break; }
        }
    }
    REQUIRE(testdata::readExtracted(seedFile, seed));

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> byteDist(0, 255);

    // A few thousand mutations of a real file: flip, insert, or delete
    // random bytes at random positions.
    for (int iter = 0; iter < 3000; iter++) {
        Blob mutated = seed;
        int mutations = 1 + static_cast<int>(rng() % 5);
        for (int m = 0; m < mutations; m++) {
            if (mutated.empty()) break;
            size_t pos = rng() % mutated.size();
            int op = rng() % 3;
            if (op == 0) {
                mutated[pos] = static_cast<u8>(byteDist(rng));
            } else if (op == 1) {
                mutated.insert(mutated.begin() + static_cast<long>(pos),
                                static_cast<u8>(byteDist(rng)));
            } else {
                mutated.erase(mutated.begin() + static_cast<long>(pos));
            }
        }
        TextFile tf = parseTextBlocks(mutated.data(), mutated.size());
        (void)tf; // termination without crashing is the whole test
    }

    // Plain random byte strings of varying lengths.
    std::uniform_int_distribution<int> lenDist(0, 4096);
    for (int iter = 0; iter < 2000; iter++) {
        int len = lenDist(rng);
        Blob randomBytes(static_cast<size_t>(len));
        for (auto& b : randomBytes) b = static_cast<u8>(byteDist(rng));
        TextFile tf = parseTextBlocks(randomBytes.data(), randomBytes.size());
        (void)tf;
    }

    CHECK(true); // reaching here means every mutation terminated
}
