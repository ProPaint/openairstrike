// as3d::extractExeTexts (as3d/exe_texts.h): tools/extract_exe_texts.py in C++.
#include "as3d/exe_texts.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#include "as3d/sha256.h"

namespace as3d {

namespace {

// One entry of a sequel's address list: kind 't' text, 'm' text with line breaks, 'u' u32.
struct ListedText {
    const char* key;
    std::uint32_t address;
    char kind;
};

// The as2 and gulf lists below are tools/exe_texts/<key>.json's entries, in order
// (apps/tests/game_import_test.cpp checks it).
// BEGIN as2 address list (generated from tools/exe_texts/as2.json)
const ListedText kAs2List[] = {
    {"title.start_game", 0x48EE84, 't'}, {"title.options", 0x48EBC4, 't'}, {"title.controls", 0x48D728, 't'},
    {"title.heli", 0x48DE98, 't'}, {"title.mission_complete", 0x48D934, 't'}, {"title.top_scores", 0x48EEDC, 't'},
    {"title.enter_name", 0x48ED24, 't'}, {"title.exit", 0x48D9A0, 't'}, {"title.hint", 0x48EEB0, 't'},
    {"title.game_over", 0x48DD34, 't'}, {"label.exit", 0x48D980, 't'}, {"label.difficulty", 0x48EE90, 't'},
    {"label.game_mode", 0x48EE9C, 't'}, {"label.player", 0x48DEAC, 't'}, {"difficulty.0", 0x48EE78, 't'},
    {"difficulty.1", 0x48EE70, 't'}, {"difficulty.2", 0x48EE68, 't'}, {"difficulty.3", 0x48EE60, 't'},
    {"difficulty.4", 0x48EE54, 't'}, {"mode.0", 0x48EE44, 't'}, {"mode.1", 0x48EE38, 't'},
    {"button.start_game", 0x48ECE8, 't'}, {"button.top_scores", 0x48ECF8, 't'}, {"button.options", 0x48EBC4, 't'},
    {"button.information", 0x48ED08, 't'}, {"button.credits", 0x48ED18, 't'}, {"button.quit", 0x48EBD0, 't'},
    {"button.quit_wide", 0x48D954, 't'}, {"button.yes", 0x48D9B0, 't'}, {"button.no", 0x48D9B8, 't'},
    {"button.back", 0x48D794, 't'}, {"button.back_wide", 0x48D8B0, 't'}, {"button.next", 0x48EEA8, 't'},
    {"button.next_wide", 0x48D974, 't'}, {"button.start", 0x48DEC0, 't'}, {"button.continue", 0x48DD28, 't'},
    {"button.accept", 0x48DEB4, 't'}, {"button.restart", 0x48D948, 't'}, {"button.resume", 0x48EBB8, 't'},
    {"button.choose_heli", 0x48D960, 't'}, {"button.configure_controls", 0x48EE18, 't'},
    {"button.apply", 0x48EE30, 't'}, {"button.ok", 0x48ED34, 't'}, {"stat.enemies", 0x48D8C8, 't'},
    {"stat.stars", 0x48D8E4, 't'}, {"stat.rank", 0x48D900, 't'}, {"msg.new_heli", 0x48D914, 't'},
    {"rank.0", 0x48B674, 't'}, {"rank.1", 0x48B66C, 't'}, {"rank.2", 0x48B65C, 't'}, {"rank.3", 0x48B654, 't'},
    {"rank.4", 0x48B644, 't'}, {"rank.5", 0x48B638, 't'}, {"rank.6", 0x48B630, 't'}, {"heli.0", 0x48DE58, 't'},
    {"heli.1", 0x48DE4C, 't'}, {"heli.2", 0x48DE3C, 't'}, {"heli.3", 0x48DE2C, 't'}, {"heli.4", 0x48DE24, 't'},
    {"heli.5", 0x48DE18, 't'}, {"heli.speed", 0x48DE88, 't'}, {"heli.armor", 0x48DE90, 't'},
    {"heli.na", 0x48DE78, 't'}, {"scores.number", 0x48EEC0, 't'}, {"scores.name", 0x48EEC4, 't'},
    {"scores.score", 0x48EECC, 't'}, {"scores.rank", 0x48EED4, 't'}, {"opt.resolution", 0x48ED90, 't'},
    {"opt.refresh", 0x48ED9C, 't'}, {"opt.depth", 0x48EDAC, 't'}, {"opt.fullscreen", 0x48EDBC, 't'},
    {"opt.brightness", 0x48EDC8, 't'}, {"opt.sfx", 0x48EDD4, 't'}, {"opt.music", 0x48EDE4, 't'},
    {"opt.sound3d", 0x48EDF4, 't'}, {"opt.camera", 0x48EE00, 't'}, {"opt.mouse", 0x48EE08, 't'},
    {"opt.refresh.default", 0x48ED80, 't'}, {"opt.off", 0x48ED7C, 't'}, {"opt.on", 0x48ED78, 't'},
    {"opt.depth.0", 0x48ED64, 't'}, {"opt.depth.16", 0x48ED44, 't'}, {"opt.depth.32", 0x48ED3C, 't'},
    {"camera.0", 0x48ED6C, 't'}, {"camera.1", 0x48ED64, 't'}, {"camera.2", 0x48ED58, 't'},
    {"camera.3", 0x48ED4C, 't'}, {"ctl.set", 0x48D784, 't'}, {"ctl.player.1", 0x48D52C, 't'},
    {"ctl.player.2", 0x48D520, 't'}, {"ctl.row.0", 0x48D6EC, 't'}, {"ctl.row.1", 0x48D6DC, 't'},
    {"ctl.row.2", 0x48D6D8, 't'}, {"ctl.row.3", 0x48D6C8, 't'}, {"ctl.row.4", 0x48D6B8, 't'},
    {"ctl.row.5", 0x48D6D8, 't'}, {"ctl.row.6", 0x48D6AC, 't'}, {"ctl.row.7", 0x48D6A0, 't'},
    {"ctl.row.8", 0x48D6D8, 't'}, {"ctl.row.9", 0x48D690, 't'}, {"info.page", 0x48EBB0, 't'},
    {"info.hint.prev", 0x48EB84, 't'}, {"info.hint.next", 0x48EB9C, 't'}, {"info.pages.1", 0x48DF10, 't'},
    {"info.pages.2", 0x48DF08, 't'}, {"info.pages.3", 0x48DF00, 't'}, {"info.pages.4", 0x48DEF8, 't'},
    {"info.pages.5", 0x48DEF0, 't'}, {"info.pages.6", 0x48DEE8, 't'}, {"info.pages.7", 0x48DEE0, 't'},
    {"info.pages.8", 0x48DED8, 't'}, {"info.1.title", 0x48E150, 't'}, {"info.1.0", 0x48DF18, 't'},
    {"info.1.1", 0x48DF60, 't'}, {"info.1.2", 0x48DFA0, 't'}, {"info.1.3", 0x48DFE0, 't'},
    {"info.1.5", 0x48DFF0, 't'}, {"info.1.6", 0x48E020, 't'}, {"info.1.8", 0x48E068, 't'},
    {"info.1.9", 0x48E0B0, 't'}, {"info.1.10", 0x48E0F8, 't'}, {"info.1.11", 0x48E13C, 't'},
    {"info.2.title", 0x48E340, 't'}, {"info.2.0", 0x48E15C, 't'}, {"info.2.1", 0x48E16C, 't'},
    {"info.2.2", 0x48E1A8, 't'}, {"info.2.3", 0x48E1E4, 't'}, {"info.2.5", 0x48E200, 't'},
    {"info.2.6", 0x48E210, 't'}, {"info.2.7", 0x48E24C, 't'}, {"info.2.9", 0x48E260, 't'},
    {"info.2.10", 0x48E270, 't'}, {"info.2.11", 0x48E2A4, 't'}, {"info.2.13", 0x48E2E0, 't'},
    {"info.2.14", 0x48E2F0, 't'}, {"info.2.15", 0x48E328, 't'}, {"info.3.title", 0x48E50C, 't'},
    {"info.3.0", 0x48E360, 't'}, {"info.3.1", 0x48E36C, 't'}, {"info.3.2", 0x48E3AC, 't'},
    {"info.3.3", 0x48E3E8, 't'}, {"info.3.4", 0x48E420, 't'}, {"info.3.6", 0x48E440, 't'},
    {"info.3.7", 0x48E450, 't'}, {"info.3.8", 0x48E48C, 't'}, {"info.3.10", 0x48E4BC, 't'},
    {"info.3.11", 0x48E4C8, 't'}, {"info.3.12", 0x48E4F8, 't'}, {"info.4.title", 0x48E5D0, 't'},
    {"info.4.0", 0x48E52C, 't'}, {"info.4.1", 0x48E53C, 't'}, {"info.4.2", 0x48E578, 't'},
    {"info.4.4", 0x48E5AC, 't'}, {"info.4.5", 0x48E5BC, 't'}, {"info.5.title", 0x48E828, 't'},
    {"info.5.0", 0x48E5F0, 't'}, {"info.5.1", 0x48E604, 't'}, {"info.5.2", 0x48E640, 't'},
    {"info.5.4", 0x48E680, 't'}, {"info.5.5", 0x48E690, 't'}, {"info.5.7", 0x48E6C8, 't'},
    {"info.5.8", 0x48E6E8, 't'}, {"info.5.9", 0x48E728, 't'}, {"info.5.10", 0x48E764, 't'},
    {"info.5.12", 0x48E76C, 't'}, {"info.5.13", 0x48E788, 't'}, {"info.5.14", 0x48E7C4, 't'},
    {"info.5.15", 0x48E7FC, 't'}, {"info.6.title", 0x48E8C0, 't'}, {"info.6.0", 0x48E840, 't'},
    {"info.6.1", 0x48E854, 't'}, {"info.6.2", 0x48E88C, 't'}, {"info.7.title", 0x48EA84, 't'},
    {"info.7.0", 0x48E8D8, 't'}, {"info.7.1", 0x48E8E8, 't'}, {"info.7.2", 0x48E924, 't'},
    {"info.7.4", 0x48E950, 't'}, {"info.7.5", 0x48E960, 't'}, {"info.7.7", 0x48E98C, 't'},
    {"info.7.8", 0x48E99C, 't'}, {"info.7.10", 0x48E9D8, 't'}, {"info.7.11", 0x48E9EC, 't'},
    {"info.7.12", 0x48EA24, 't'}, {"info.7.13", 0x48EA5C, 't'}, {"info.8.title", 0x48EB70, 't'},
    {"info.8.0", 0x48EA98, 't'}, {"info.8.1", 0x48EAAC, 't'}, {"info.8.2", 0x48EAE0, 't'},
    {"info.8.4", 0x48EB10, 't'}, {"info.8.5", 0x48EB20, 't'}, {"info.8.6", 0x48EB4C, 't'},
    {"credits.0", 0x48D79C, 't'}, {"credits.1", 0x48D7AC, 't'}, {"credits.3", 0x48D7C0, 't'},
    {"credits.4", 0x48D7CC, 't'}, {"credits.6", 0x48D7DC, 't'}, {"credits.7", 0x48D7F8, 't'},
    {"credits.8", 0x48D808, 't'}, {"credits.10", 0x48D81C, 't'}, {"credits.11", 0x48D830, 't'},
    {"credits.13", 0x48D840, 't'}, {"credits.14", 0x48D850, 't'}, {"credits.16", 0x48D864, 't'},
    {"credits.17", 0x48D86C, 't'}, {"credits.19", 0x48D888, 't'}, {"credits.20", 0x48D898, 't'},
    {"congrats.0", 0x48DB10, 't'}, {"congrats.2", 0x48DB24, 't'}, {"congrats.3", 0x48DB60, 't'},
    {"congrats.4", 0x48DB9C, 't'}, {"congrats.6", 0x48DBD0, 't'}, {"congrats.7", 0x48DC14, 't'},
    {"congrats.8", 0x48DC50, 't'}, {"congrats.10", 0x48DC68, 't'}, {"loading.label", 0x48A424, 't'},
    {"cheat.god_on", 0x48A018, 't'}, {"cheat.god_off", 0x48A02C, 't'}, {"cheat.lives", 0x48A054, 't'},
    {"cheat.weapons", 0x48A07C, 't'}, {"cheat.missiles", 0x48A0A4, 't'}, {"cheat.powerups", 0x48A0D0, 't'},
    {"dialog.1.start.0", 0x48D460, 'm'}, {"dialog.1.start.0.speaker", 0x49D32C, 'u'},
    {"dialog.1.start.1", 0x48D41C, 'm'}, {"dialog.1.start.1.speaker", 0x49D334, 'u'},
    {"dialog.1.end.0", 0x48D3EC, 'm'}, {"dialog.1.end.0.speaker", 0x49D344, 'u'}, {"dialog.2.start.0", 0x48D370, 'm'},
    {"dialog.2.start.0.speaker", 0x49D354, 'u'}, {"dialog.2.start.1", 0x48D360, 't'},
    {"dialog.2.start.1.speaker", 0x49D35C, 'u'}, {"dialog.2.end.0", 0x48D2E0, 'm'},
    {"dialog.2.end.0.speaker", 0x49D36C, 'u'}, {"dialog.3.start.0", 0x48D258, 'm'},
    {"dialog.3.start.0.speaker", 0x49D37C, 'u'}, {"dialog.3.start.1", 0x48D1E8, 'm'},
    {"dialog.3.start.1.speaker", 0x49D384, 'u'}, {"dialog.3.start.2", 0x48D168, 'm'},
    {"dialog.3.start.2.speaker", 0x49D38C, 'u'}, {"dialog.3.end.0", 0x48D148, 't'},
    {"dialog.3.end.0.speaker", 0x49D39C, 'u'}, {"dialog.4.end.0", 0x48D13C, 't'},
    {"dialog.4.end.0.speaker", 0x49D3AC, 'u'}, {"dialog.5.start.0", 0x48D0A8, 'm'},
    {"dialog.5.start.0.speaker", 0x49D3BC, 'u'}, {"dialog.5.end.0", 0x48D058, 'm'},
    {"dialog.5.end.0.speaker", 0x49D3CC, 'u'}, {"dialog.6.start.0", 0x48CFD8, 'm'},
    {"dialog.6.start.0.speaker", 0x49D3DC, 'u'}, {"dialog.6.start.1", 0x48CF70, 'm'},
    {"dialog.6.start.1.speaker", 0x49D3E4, 'u'}, {"dialog.6.end.0", 0x48CF30, 'm'},
    {"dialog.6.end.0.speaker", 0x49D3F4, 'u'}, {"dialog.6.end.1", 0x48CF0C, 'm'},
    {"dialog.6.end.1.speaker", 0x49D3FC, 'u'}, {"dialog.8.start.0", 0x48CE90, 'm'},
    {"dialog.8.start.0.speaker", 0x49D40C, 'u'}, {"dialog.8.start.1", 0x48CDE8, 'm'},
    {"dialog.8.start.1.speaker", 0x49D414, 'u'}, {"dialog.8.start.2", 0x48CDB0, 'm'},
    {"dialog.8.start.2.speaker", 0x49D41C, 'u'}, {"dialog.9.end.0", 0x48CD98, 't'},
    {"dialog.9.end.0.speaker", 0x49D42C, 'u'}, {"dialog.10.end.0", 0x48CD70, 't'},
    {"dialog.10.end.0.speaker", 0x49D43C, 'u'}, {"dialog.10.end.1", 0x48CD20, 'm'},
    {"dialog.10.end.1.speaker", 0x49D444, 'u'}, {"dialog.11.start.0", 0x48CC78, 'm'},
    {"dialog.11.start.0.speaker", 0x49D454, 'u'}, {"dialog.11.start.1", 0x48CC58, 'm'},
    {"dialog.11.start.1.speaker", 0x49D45C, 'u'}, {"dialog.11.end.0", 0x48CC00, 'm'},
    {"dialog.11.end.0.speaker", 0x49D46C, 'u'}, {"dialog.12.start.0", 0x48CB58, 'm'},
    {"dialog.12.start.0.speaker", 0x49D47C, 'u'}, {"dialog.12.end.0", 0x48CB38, 't'},
    {"dialog.12.end.0.speaker", 0x49D48C, 'u'}, {"dialog.12.end.1", 0x48CB0C, 't'},
    {"dialog.12.end.1.speaker", 0x49D494, 'u'}, {"dialog.14.start.0", 0x48CAC8, 'm'},
    {"dialog.14.start.0.speaker", 0x49D4A4, 'u'}, {"dialog.14.start.1", 0x48CAA0, 't'},
    {"dialog.14.start.1.speaker", 0x49D4AC, 'u'}, {"dialog.15.start.0", 0x48CA50, 'm'},
    {"dialog.15.start.0.speaker", 0x49D4BC, 'u'}, {"dialog.15.start.1", 0x48CA44, 't'},
    {"dialog.15.start.1.speaker", 0x49D4C4, 'u'}, {"dialog.15.end.0", 0x48C9D0, 'm'},
    {"dialog.15.end.0.speaker", 0x49D4D4, 'u'}, {"dialog.16.end.0", 0x48C9A8, 'm'},
    {"dialog.16.end.0.speaker", 0x49D4E4, 'u'}, {"dialog.17.start.0", 0x48C960, 'm'},
    {"dialog.17.start.0.speaker", 0x49D4F4, 'u'}, {"dialog.17.start.1", 0x48C91C, 'm'},
    {"dialog.17.start.1.speaker", 0x49D4FC, 'u'}, {"dialog.18.start.0", 0x48C890, 'm'},
    {"dialog.18.start.0.speaker", 0x49D50C, 'u'}, {"dialog.18.start.1", 0x48C830, 'm'},
    {"dialog.18.start.1.speaker", 0x49D514, 'u'}, {"dialog.18.start.2", 0x48C7F4, 'm'},
    {"dialog.18.start.2.speaker", 0x49D51C, 'u'},
};
// END as2 address list

// BEGIN gulf address list (generated from tools/exe_texts/gulf.json)
const ListedText kGulfList[] = {
    {"title.start_game", 0x48CBC4, 't'}, {"title.options", 0x48CA7C, 't'}, {"title.controls", 0x48B6A0, 't'},
    {"title.heli", 0x48BE00, 't'}, {"title.mission_complete", 0x48B8C8, 't'}, {"title.top_scores", 0x48CBD0, 't'},
    {"title.enter_name", 0x48CBF0, 't'}, {"title.exit", 0x48B93C, 't'}, {"title.hint", 0x48CD70, 't'},
    {"title.game_over", 0x48BC90, 't'}, {"label.exit", 0x48B91C, 't'}, {"label.difficulty", 0x48CD58, 't'},
    {"label.game_mode", 0x48CD64, 't'}, {"label.player", 0x48BE14, 't'}, {"difficulty.0", 0x48CD4C, 't'},
    {"difficulty.1", 0x48CD44, 't'}, {"difficulty.2", 0x48CD3C, 't'}, {"difficulty.3", 0x48CD34, 't'},
    {"difficulty.4", 0x48CD28, 't'}, {"mode.0", 0x48CD18, 't'}, {"mode.1", 0x48CD0C, 't'},
    {"button.start_game", 0x48CBC4, 't'}, {"button.top_scores", 0x48CBD0, 't'}, {"button.options", 0x48CA7C, 't'},
    {"button.information", 0x48CBDC, 't'}, {"button.credits", 0x48CBE8, 't'}, {"button.quit", 0x48CA8C, 't'},
    {"button.quit_wide", 0x48B8EC, 't'}, {"button.yes", 0x48B94C, 't'}, {"button.no", 0x48B954, 't'},
    {"button.back", 0x48B70C, 't'}, {"button.back_wide", 0x48B70C, 't'}, {"button.next", 0x48B910, 't'},
    {"button.next_wide", 0x48B910, 't'}, {"button.start", 0x48BE34, 't'}, {"button.continue", 0x48BC80, 't'},
    {"button.continue.heli", 0x48BE28, 't'}, {"button.accept", 0x48BE1C, 't'}, {"button.restart", 0x48B8DC, 't'},
    {"button.restart.gameover", 0x48BD74, 't'}, {"button.resume", 0x48CA74, 't'},
    {"button.choose_heli", 0x48B8F8, 't'}, {"button.configure_controls", 0x48CCE8, 't'},
    {"button.apply", 0x48CD00, 't'}, {"button.ok", 0x48CC00, 't'}, {"button.hint_ok", 0x48CD80, 't'},
    {"stat.enemies", 0x48B85C, 't'}, {"stat.stars", 0x48B878, 't'}, {"stat.rank", 0x48B894, 't'},
    {"msg.new_heli", 0x48B8A8, 't'}, {"rank.0", 0x489F64, 't'}, {"rank.1", 0x489F5C, 't'}, {"rank.2", 0x489F4C, 't'},
    {"rank.3", 0x489F44, 't'}, {"rank.4", 0x489F34, 't'}, {"rank.5", 0x489F28, 't'}, {"rank.6", 0x489F20, 't'},
    {"heli.0", 0x48BDC0, 't'}, {"heli.1", 0x48BDB4, 't'}, {"heli.2", 0x48BDA4, 't'}, {"heli.speed", 0x48BDF0, 't'},
    {"heli.armor", 0x48BDF8, 't'}, {"heli.na", 0x48BDE0, 't'}, {"scores.number", 0x48CD8C, 't'},
    {"scores.name", 0x48CD90, 't'}, {"scores.score", 0x48CD98, 't'}, {"scores.rank", 0x48CDA0, 't'},
    {"opt.resolution", 0x48CC60, 't'}, {"opt.refresh", 0x48CC6C, 't'}, {"opt.depth", 0x48CC7C, 't'},
    {"opt.fullscreen", 0x48CC8C, 't'}, {"opt.brightness", 0x48CC98, 't'}, {"opt.sfx", 0x48CCA4, 't'},
    {"opt.music", 0x48CCB4, 't'}, {"opt.sound3d", 0x48CCC4, 't'}, {"opt.camera", 0x48CCD0, 't'},
    {"opt.mouse", 0x48CCD8, 't'}, {"opt.refresh.default", 0x48CC50, 't'}, {"opt.off", 0x48CC4C, 't'},
    {"opt.on", 0x48CC48, 't'}, {"opt.depth.0", 0x48CC34, 't'}, {"opt.depth.16", 0x48CC14, 't'},
    {"opt.depth.32", 0x48CC0C, 't'}, {"camera.0", 0x48CC3C, 't'}, {"camera.1", 0x48CC34, 't'},
    {"camera.2", 0x48CC28, 't'}, {"camera.3", 0x48CC1C, 't'}, {"ctl.set", 0x48B6FC, 't'},
    {"ctl.player.1", 0x48B4A4, 't'}, {"ctl.player.2", 0x48B498, 't'}, {"ctl.row.0", 0x48B664, 't'},
    {"ctl.row.1", 0x48B654, 't'}, {"ctl.row.2", 0x48B640, 't'}, {"ctl.row.3", 0x48B630, 't'},
    {"ctl.row.4", 0x48B624, 't'}, {"ctl.row.5", 0x48B618, 't'}, {"ctl.row.6", 0x48B608, 't'},
    {"ctl.row.7", 0x48B5F8, 't'}, {"ctl.row.8", 0x48B5EC, 't'}, {"ctl.row.9", 0x48B5E0, 't'},
    {"info.page", 0x48CA6C, 't'}, {"info.hint.prev", 0x48CA40, 't'}, {"info.hint.next", 0x48CA58, 't'},
    {"info.pages.1", 0x48BE80, 't'}, {"info.pages.2", 0x48BE78, 't'}, {"info.pages.3", 0x48BE70, 't'},
    {"info.pages.4", 0x48BE68, 't'}, {"info.pages.5", 0x48BE60, 't'}, {"info.pages.6", 0x48BE58, 't'},
    {"info.pages.7", 0x48BE50, 't'}, {"info.1.title", 0x48C0D0, 't'}, {"info.1.0", 0x48BE88, 't'},
    {"info.1.1", 0x48BED0, 't'}, {"info.1.2", 0x48BF10, 't'}, {"info.1.3", 0x48BF50, 't'},
    {"info.1.5", 0x48BF60, 't'}, {"info.1.6", 0x48BFA0, 't'}, {"info.1.8", 0x48BFE8, 't'},
    {"info.1.9", 0x48C030, 't'}, {"info.1.10", 0x48C078, 't'}, {"info.1.11", 0x48C0BC, 't'},
    {"info.2.title", 0x48C2C0, 't'}, {"info.2.0", 0x48C0DC, 't'}, {"info.2.1", 0x48C0EC, 't'},
    {"info.2.2", 0x48C128, 't'}, {"info.2.3", 0x48C164, 't'}, {"info.2.5", 0x48C180, 't'},
    {"info.2.6", 0x48C190, 't'}, {"info.2.7", 0x48C1CC, 't'}, {"info.2.9", 0x48C1E0, 't'},
    {"info.2.10", 0x48C1F0, 't'}, {"info.2.11", 0x48C224, 't'}, {"info.2.13", 0x48C260, 't'},
    {"info.2.14", 0x48C270, 't'}, {"info.2.15", 0x48C2A8, 't'}, {"info.3.title", 0x48C48C, 't'},
    {"info.3.0", 0x48C2E0, 't'}, {"info.3.1", 0x48C2EC, 't'}, {"info.3.2", 0x48C32C, 't'},
    {"info.3.3", 0x48C368, 't'}, {"info.3.4", 0x48C3A0, 't'}, {"info.3.6", 0x48C3C0, 't'},
    {"info.3.7", 0x48C3D0, 't'}, {"info.3.8", 0x48C40C, 't'}, {"info.3.10", 0x48C43C, 't'},
    {"info.3.11", 0x48C448, 't'}, {"info.3.12", 0x48C478, 't'}, {"info.5.title", 0x48C6E4, 't'},
    {"info.5.0", 0x48C4AC, 't'}, {"info.5.1", 0x48C4C0, 't'}, {"info.5.2", 0x48C4FC, 't'},
    {"info.5.4", 0x48C53C, 't'}, {"info.5.5", 0x48C54C, 't'}, {"info.5.7", 0x48C584, 't'},
    {"info.5.8", 0x48C5A4, 't'}, {"info.5.9", 0x48C5E4, 't'}, {"info.5.10", 0x48C620, 't'},
    {"info.5.12", 0x48C628, 't'}, {"info.5.13", 0x48C644, 't'}, {"info.5.14", 0x48C680, 't'},
    {"info.5.15", 0x48C6B8, 't'}, {"info.6.title", 0x48C77C, 't'}, {"info.6.0", 0x48C6FC, 't'},
    {"info.6.1", 0x48C710, 't'}, {"info.6.2", 0x48C748, 't'}, {"info.7.title", 0x48C940, 't'},
    {"info.7.0", 0x48C794, 't'}, {"info.7.1", 0x48C7A4, 't'}, {"info.7.2", 0x48C7E0, 't'},
    {"info.7.4", 0x48C80C, 't'}, {"info.7.5", 0x48C81C, 't'}, {"info.7.7", 0x48C848, 't'},
    {"info.7.8", 0x48C858, 't'}, {"info.7.10", 0x48C894, 't'}, {"info.7.11", 0x48C8A8, 't'},
    {"info.7.12", 0x48C8E0, 't'}, {"info.7.13", 0x48C918, 't'}, {"info.8.title", 0x48CA2C, 't'},
    {"info.8.0", 0x48C954, 't'}, {"info.8.1", 0x48C968, 't'}, {"info.8.2", 0x48C99C, 't'},
    {"info.8.4", 0x48C9CC, 't'}, {"info.8.5", 0x48C9DC, 't'}, {"info.8.6", 0x48CA08, 't'},
    {"credits.0", 0x48B718, 't'}, {"credits.1", 0x48B728, 't'}, {"credits.3", 0x48B73C, 't'},
    {"credits.4", 0x48B748, 't'}, {"credits.5", 0x48B758, 't'}, {"credits.7", 0x48B768, 't'},
    {"credits.8", 0x48B784, 't'}, {"credits.9", 0x48B794, 't'}, {"credits.11", 0x48B7A8, 't'},
    {"credits.12", 0x48B7BC, 't'}, {"credits.14", 0x48B7CC, 't'}, {"credits.15", 0x48B7DC, 't'},
    {"credits.16", 0x48B7F0, 't'}, {"credits.18", 0x48B804, 't'}, {"credits.19", 0x48B80C, 't'},
    {"credits.21", 0x48B828, 't'}, {"credits.22", 0x48B838, 't'}, {"congrats.0", 0x48BA68, 't'},
    {"congrats.2", 0x48BA7C, 't'}, {"congrats.3", 0x48BAB8, 't'}, {"congrats.4", 0x48BAF4, 't'},
    {"congrats.6", 0x48BB28, 't'}, {"congrats.7", 0x48BB6C, 't'}, {"congrats.8", 0x48BBA8, 't'},
    {"congrats.10", 0x48BBC0, 't'}, {"loading.label", 0x4892EC, 't'}, {"cheat.god_on", 0x488FF8, 't'},
    {"cheat.god_off", 0x48900C, 't'}, {"cheat.lives", 0x489034, 't'}, {"cheat.weapons", 0x48905C, 't'},
    {"cheat.missiles", 0x489084, 't'}, {"cheat.powerups", 0x4890B0, 't'}, {"dialog.10.start.0", 0x48B3E0, 'm'},
    {"dialog.10.start.0.speaker", 0x49B324, 'u'}, {"dialog.10.start.1", 0x48B340, 'm'},
    {"dialog.10.start.1.speaker", 0x49B32C, 'u'}, {"dialog.10.start.2", 0x48B2B8, 'm'},
    {"dialog.10.start.2.speaker", 0x49B334, 'u'}, {"dialog.10.end.0", 0x48B250, 'm'},
    {"dialog.10.end.0.speaker", 0x49B344, 'u'}, {"dialog.10.end.1", 0x48B228, 'm'},
    {"dialog.10.end.1.speaker", 0x49B34C, 'u'}, {"dialog.24.start.0", 0x48B160, 'm'},
    {"dialog.24.start.0.speaker", 0x49B35C, 'u'}, {"dialog.24.start.1", 0x48B0E8, 'm'},
    {"dialog.24.start.1.speaker", 0x49B364, 'u'},
};
// END gulf address list

// AirStrike 2: the control-row names read the row table as ten records; it has thirteen, three
// of them "-" separators, so the ten action names are at these addresses instead
// (docs/spec/as2/issues/300-frontend-implementation-findings.md).
const std::uint32_t kAs2CtlRows[10] = {0x48D6EC, 0x48D6DC, 0x48D6C8, 0x48D6B8, 0x48D6AC,
                                       0x48D6A0, 0x48D690, 0x48D680, 0x48D674, 0x48D668};

// The hash dictionaries (docs/spec/issues/170, section 3): for every key of a game's address
// table, the SHA-256 of the text's cp1251 bytes and its length, or a u32's value (sha256
// empty). Generated from tools/exe_texts/<key>.hashes.json by
// `tools/extract_exe_texts.py --write-tables` (apps/tests/game_import_test.cpp checks them);
// the strings themselves are not here. Any build of a game gives its texts through them.
struct HashedText {
    const char* key;
    const char* sha256;
    std::uint16_t len;
    std::uint32_t value;
};
const HashedText kAs3dHashes[] = {
    // BEGIN as3d hash list (generated from tools/exe_texts/as3d.hashes.json)
    {"info.1.title", "ed1d3c4f791a305df7cf604ed62e7cea5832babce21fe97d7237a786b1a71dd9", 23, 0},
    {"info.1.0", "1da82a7eda7bea8915c60b32c19611b41879a42139a59d42083051d35d90a79e", 49, 0},
    {"info.1.1", "01df86f8b3f85a170e59d6e6cc45a5c9d4836f3f50a91e6b8541311237da4dcc", 69, 0},
    {"info.1.2", "2a2252159a0b24953fd6b90ab4b604a734570d4e2e2d8f1e6d8eb923f7ed14ba", 69, 0},
    {"info.1.3", "e765a55c8f6c68887d774623eeb0134e4dec1bbd5fb1cab347939fb282168e77", 73, 0},
    {"info.1.4", "01f0e03bf156abd7eeee43e10c80dd7bf0e511befe4e209289e39437b6a7d6f8", 20, 0},
    {"info.1.6", "f935c20dbe220891224112eebdc4a23f1f437665a94ef8776328794c361eb475", 72, 0},
    {"info.1.7", "ceed58dc00d9e58d17d2999d8677ce5c2e7a218bb41375bc13009e2ac4858ee2", 38, 0},
    {"info.1.9", "d4fa6a47ac5f1f68bc979a0d29f6a84c019e69623a7eeb79417cb684f392cbe2", 70, 0},
    {"info.1.10", "0498e7b5d059523b1d3efef44b429945c8d49e885ef980ddcf53289e63239d73", 69, 0},
    {"info.1.11", "2bbccd7958bea3622b4a24fd492a98c8a404ad31953073a5b6ac0e89433f1516", 66, 0},
    {"info.1.12", "77ccab52996ca8ff49640c0a4d124a41a8b0dd25f4cee917fab418bce0b5b9f0", 72, 0},
    {"info.1.13", "1289abbc952d2f6b9dec7e7aecd0cc6d94d46e777bb4be282e58424c6a990e0b", 74, 0},
    {"info.1.14", "c7b3c1e67594f66fe4dac53cd0588bfe15ded0c22da8855542cf51be2b5a3c45", 58, 0},
    {"info.2.title", "8aaf1911468fb5fcf5813f1e629a8e5fa8953b6db43dbf8753bea92b813cc20b", 23, 0},
    {"info.2.0", "6b533f3c9a77d1951d69162cea08cc8569a18323da65e07050f8b639f323b1e6", 64, 0},
    {"info.2.1", "9a7a473d4f9526c3eaf02a8735cc881d6299f05e4df4d32397fd9d2fcfe22b7a", 64, 0},
    {"info.2.2", "2a4821278c24380ba22ab1a037ba93043458039a1ea32951ce18733dbe6294b4", 66, 0},
    {"info.2.3", "08698422865702e99d6984aeb1b5579c4ea9987939e66376e414435fd2eecee0", 64, 0},
    {"info.2.4", "7b4dfb7a3736e130f6a81664692b6db8c3c9344146ddce6f04014b949d5f6abc", 63, 0},
    {"info.2.5", "7aae8642de53e03e61a52c00fc5d6ae56f162d6bc05d9c2fc832172883e09498", 67, 0},
    {"info.2.6", "a1ada03083fdb9a386f1feb9f1dce002327051eb42e32fe528ea11edd181269a", 68, 0},
    {"info.2.7", "0b3de632906130c7c1027a4a5e4f39573800c7b6a660d1517ace84bd7493d0fa", 14, 0},
    {"info.2.9", "c01a4377582d3b28762e992f53775990e59947e167c80c404b2ca8406bf2304e", 31, 0},
    {"info.3.title", "709276c8392f940bfd855824583be68f48be48f22a303008784cc51aebc2c08f", 8, 0},
    {"info.3.0", "88ccb2ad8b3971c4ce9bdcb6b717fde199730a5d3503372abe4685c7dded7337", 63, 0},
    {"info.3.1", "56ac180acdf5d42baa570cc1c40eff9458866b036e1b741920b4d6c10f2ecc4c", 63, 0},
    {"info.3.2", "ce39380fdff731ce0a9819ed6d925cc57a42e8ded8fe1452ceacfef1d16823c1", 62, 0},
    {"info.3.3", "c8c73f3fb838babc995b4b9561e763402798b4a386cd8f2c41333a1d5c86eda1", 15, 0},
    {"info.3.5", "66d794f559a5723a193584444856895a9cb5b723cf69318d55de3db17273e2ea", 57, 0},
    {"info.3.6", "d48e98a15472f7c7ca9d40ebec072df71655a5e30121720994644fba8fbe27ef", 71, 0},
    {"info.3.8", "f0fe3fa39c4ab78da6b7994a19193c347c42ba4ed87691ab32bc544079ccc546", 66, 0},
    {"info.3.9", "41fe95f1f5d4d167439cf250d215e52da5f5f403f6d665dfa98ebb927375b832", 66, 0},
    {"info.3.10", "35133d61aabaa8605fd5e2ab7e9aeb1970bfc733c9c5e4ccf59de36a9993f78f", 66, 0},
    {"info.3.11", "221049039a1d8896f7a8d8fe4efe378efbb7fd4647722373293c3c490179c44b", 17, 0},
    {"info.4.title", "aa487c32011d30f244572b1bbd961eee238fa13a941d1b345adacbefa1f29a46", 29, 0},
    {"info.4.0", "e9f75b3efdb9aae85a6d7844d7a0b4246ad7694095078f285edacb64cbf7f93d", 13, 0},
    {"info.4.1", "955edf1aa76c3be12b7997afff63762eff8a5be0da714a04837bd88350c41e4a", 59, 0},
    {"info.4.2", "97a38cdcc38f73053efd5bf6d71086394c96b4fb6229d650a4b3fda61fec00a5", 57, 0},
    {"info.4.4", "940180ea9dae63794c24616acb7083afa6078e1ccf5b7515b9d27d69611a7fe9", 13, 0},
    {"info.4.5", "205c1f2be9f2e8c5360c18831dcbc4d3d3b6fea7791d1c652e77a3ebf7de61d7", 57, 0},
    {"info.4.6", "e32c48d3d2cde375fbd1b4b2715a64d3e2f760f05c97750aad6f16bf6d65802d", 17, 0},
    {"info.4.8", "66bc482924d34f836f8ba8cf99d3c15cd0a203a926d825a08ff3eb7d465d680a", 15, 0},
    {"info.4.9", "0cb3aacccf509b393be6c20f8563319f26e8e9466debf8552a6f8aa2e2369ca6", 59, 0},
    {"info.4.10", "e8e3912c2ade1619d6b88be6d177c6c9134a5e7c0101f7cbe5c86e685ff195a1", 48, 0},
    {"info.4.12", "d592f9bc732480fb01e73f4af5f4919d5ec84851e2ba9b382bd8e6ad8caac0df", 11, 0},
    {"info.4.13", "a6fff1163b0b1fd67791e7c14150790d5c9873106222e39b098975b42a8d88e2", 54, 0},
    {"info.4.14", "8f260b48bdeb510a113d0199c94248c082826459cc72f33183e639c853c04b1d", 14, 0},
    {"info.5.title", "d7a7fe75311118fd386fbcf9a7a86149408b0abfa15130ddc2294fdaf33ecc56", 29, 0},
    {"info.5.0", "db631c7c44ec36f38c99431cb62654abb5ce6353cd96de931b05be0392a2a406", 16, 0},
    {"info.5.1", "b0ec4ca3e671a59081a65dd769dda8efbac29431cf20f7ef0c21d17e0ec4ad83", 59, 0},
    {"info.5.2", "66a52574c885ed1eb812060ff03a9beedd7bbd681062fa209df72bb803e8feeb", 67, 0},
    {"info.5.4", "0a42ed8407570d7699a97a45eafc8de20d935701bdc95e05c4d0492b70255c46", 12, 0},
    {"info.5.5", "c6cd16f640ae212495cda8c9061ebafa2f11513428997effb8b787cf355ce9ae", 60, 0},
    {"info.5.7", "18334511700cd9459f2fb31daff66453ed83cb6f0ea4e7fdffdbae72b461d200", 7, 0},
    {"info.5.8", "8a22e5dffdff77beabdd3103e07eacb3301cbaa1b8e215bb714c65bdb7017327", 61, 0},
    {"info.5.9", "896b12ad6ecb9c4d06b5c022d9f0f199547999b42ad3f1f75badacf886a21b98", 56, 0},
    {"info.5.10", "321151be41bd3f42088eee1246656a9e27fbba2d8632dc4349ebc53637d40d95", 62, 0},
    {"info.5.11", "2a359ae99d0e7780ccaa3cfd7d4ca243c104e8f262453eb0e89f03c855f2523e", 21, 0},
    {"info.5.13", "a67da37b00808c479ae85d40680b31aa3faa6cfb607ea14094d1f2dc1debbb85", 10, 0},
    {"info.5.14", "5b8358d99bf71de067c35457c564f3ab5bf21e4067d91e21d4b1c8f9beb60399", 61, 0},
    {"info.5.15", "60192163b79bf51b0fa8aa4d0586acec1a478349c822a825442360df3743815b", 22, 0},
    {"info.6.title", "3c75037a1bd2285262650c3780a71756553c0db18e5c77b188949dff12562942", 29, 0},
    {"info.6.0", "91fa18dae59c434049456ec0a80cb265f410728965e5f4f8d6c2611134204c20", 15, 0},
    {"info.6.1", "fc529c4c1c5b5f60d7e11a9b4880f8e6a390c02d0e92f94eaa405e43b7bcfcdc", 59, 0},
    {"info.6.2", "80e2fffade4f37622a81342a30b0da289d0c3c3355990b7f7f3a254fc69c81d8", 20, 0},
    {"info.6.4", "0f6cc450027675b55b965dd96e43ea46206014bb0b9ae140c3b47271cf8073a5", 14, 0},
    {"info.6.5", "99e010bcf4b1a3ceaf8c0c601ed335fddc6b1edf3e9688dedd0f6320e335fba9", 17, 0},
    {"info.7.title", "3bc9c521b514763db1982ee4b5aaa712cf8c77c94a85eb4a22cae9aefb2183cd", 22, 0},
    {"info.7.0", "bade0e396be81863cdd900fe9ec70e666ac2ea88f04027c06115601d95717c74", 16, 0},
    {"info.7.1", "5339212530f36a34bb534c5be9515b5889398393fe95da0b175a6288ff08be7e", 58, 0},
    {"info.7.2", "1a97be088ae53fd404e3f055a6654d415f180ad0e9841b81a8b56799bdd2f122", 60, 0},
    {"info.7.4", "a770689b6125453b05b771b1d6d5797070cfb904760b704b0b8e8022499d7afd", 14, 0},
    {"info.7.5", "e762b3679bab00281936c50fa310316356b87c9bcd0ed965b0467b6498724419", 53, 0},
    {"info.7.7", "88ee541402d0e65d8afc3f4ede2cb1e0d9dea94424fe99719e8f10ab29345c74", 29, 0},
    {"info.7.8", "1230d1642a97d823005e5dd5e81a45a04f5a1883291259c6357f57275afb8f18", 58, 0},
    {"info.7.9", "cc38a7acb0709496c6799e1af626c44e6c7c63eddcb03f58b5134fcc3c02093b", 57, 0},
    {"info.7.10", "a85faa09e43d645115cbd4d2267d51d2b8954e117c7cf683d03726f1f3c75238", 7, 0},
    {"info.7.12", "d7fd51fae0c6a21f9d5e3f8d37f15058bb7d8fd0021dece553bc92ad9c037ff5", 27, 0},
    {"info.7.13", "3fd100e58fab04e0bc128408ef3f211bbef6cbc816807556efd50b95ae540ef1", 59, 0},
    {"info.7.14", "c88ee7f14db815d2ace7a570a48989715d4a0c921df527310b2f1991708a0368", 54, 0},
    {"info.7.15", "2e42fb8b3e0da71eea57dae17df4820ac6f83c536eab159207ec71df70b6fe12", 40, 0},
    {"info.8.title", "9e469dbdcbe64d0de8dd2d12317bc42c6750d15c1987cde5e3668502d3a0a522", 22, 0},
    {"info.8.0", "d299498dd3952632bdbf0e5c609afef0b32241ac48680c21cb036dc52a9e723e", 17, 0},
    {"info.8.1", "c5242450f016f4b72b146899baaea921c2dc37f84aad2bde7bb7f202b3ba9f32", 55, 0},
    {"info.8.2", "424e95bdd9b2b365664fc2d739d9dfeb0d44d38a5e89248a73df40185f5ca5b1", 51, 0},
    {"info.9.title", "29880ed052f699700a5e91a7af3506ba233bd4f6bd20fe42625f94b49460c5ba", 5, 0},
    {"info.9.0", "bc86549da7105fbcf60b85d29b788004a926b7826907232d6a75f366f209a71f", 14, 0},
    {"info.9.1", "172f4dc9a861896415b8b999b8cc1f80306a7c7ce9d8c7dcf63a026c5e34e1ad", 58, 0},
    {"info.9.2", "c8633f0f517a93b825ecf0b7d5e9b1726f0fbb8786a82cb29db71fc30bf06c10", 61, 0},
    {"info.9.4", "e0603a89ecb713fc40b4193412294aca22f3f3d8b8253cef345ca83a6c5a7dbc", 14, 0},
    {"info.9.5", "632d9e43b1886aeba209ba56d2dbb2462128918093128fdfd40a4b4a5cc48caa", 39, 0},
    {"info.9.7", "22c49d44a0299584a24a11c86a020aa0c43418f495e3cb56e6517caebd4e75d6", 15, 0},
    {"info.9.8", "4c435d78e1c10b8750d97d04d85ea568f4b8192bcea22ed22ade5e8ace259d09", 59, 0},
    {"info.9.10", "1de03c7866277d05acff9f84af65d7ebb8899c4ed2a011f1e63df87296d24eb0", 16, 0},
    {"info.9.11", "953cc152a0f86d0f9ed3b973a60f6a46d50a8bc7a93b34007463e2724d0bb621", 59, 0},
    {"info.9.12", "d16e8599863a8e2e51f587be1b89e59921d9ac5de50aaea3384f6f83dbc5248d", 55, 0},
    {"info.9.13", "11190007e42062fb0c3bb74906ae85e8f03214a8065dd6f2f19aa1a027f69466", 21, 0},
    {"info.10.title", "4325a27b45a8f14e8131fcf8d6dc321d483de0b3e3cd4e1fed2e90846e934f7c", 7, 0},
    {"info.10.4", "7813d8075fd6bab9c439bf9c92fcb1321870d4425515ce365ccc7237fd01cf1a", 11, 0},
    {"info.10.5", "fb805d81d0b9bc58c3a64d5fdb68a04bd52198c61f9ab86354f56de6d77ed1f1", 12, 0},
    {"info.10.7", "b379d0b981f51aae0d3da670fd839098e591df1ffeada0855829b9cab8ffaccf", 18, 0},
    {"info.10.8", "5081f9f23008e79533525d85ef6373aeaceef20e45c2337c0f5ef70b8d3a4f2b", 15, 0},
    {"info.10.10", "ac8fca60214a65f7c4894650b7663fa5a4d8b33d28763590a15c3d93fc2ebe2a", 13, 0},
    {"info.10.11", "98bee43b61ad53744ba91ce0246bea87a3d13052417dbece1e002f0fd502555d", 18, 0},
    {"info.10.13", "08d5f92ca9e9cecc2807e89018f28f8d705476ab927b7c4ca79bda0efbe4baab", 5, 0},
    {"info.10.14", "9add605e0ce7423a2be72ffcb9b0d1fc4584ae8fb944ca7082c01db469429244", 22, 0},
    {"congrats.0", "6a9fac33b7c1e18c95bd33f3daeeb5dc4e9589257ad2ea8e1a6ee2bf2a9bb181", 16, 0},
    {"congrats.2", "e6da6cbbf34799b65e47e9a0f412be20e14ac6859812af71dca4349866a14dbb", 56, 0},
    {"congrats.3", "cb433b32a48903e73cdf7380f50e208cfbbf25293d07a3a8dc153fddf846e588", 50, 0},
    {"congrats.4", "3ecf2d6a6d79b856043475225abb4e024572209324fce44dc1ef694a0991fdb0", 52, 0},
    {"rank.0", "3e15506b1ce2ee5e6c14e77df09c92476af59252bcac021cd9d029fd41125499", 7, 0},
    {"rank.1", "ea0bedb7c8b0f3cb364add8740dc5800838ff4aa0fe39458d1fbaab6afa00bd4", 6, 0},
    {"rank.2", "d8dc750ee2b2f28e16f02b090423299b3db49292e3b4b7f9c16551a64e4ce9e1", 12, 0},
    {"rank.3", "383e2d946278a872c3723cb83a961d8c4809df7110628cca0ba1a808267b3561", 5, 0},
    {"rank.4", "a93403d1eafa636fd871adc6aa56c36ca55642e2deee98a8d5e15906b3d1de58", 12, 0},
    {"rank.5", "143f40fdffa420df4a569d09f72cdb60bf2c2e5a6a8a0bd3cebb1effa8523177", 9, 0},
    {"rank.6", "1d6953e03ac29b9aba73dfcd57d8a5cb47991342495891f19b1cb713da89eb5d", 5, 0},
    {"info.hint.prev", "99d9b5f5a15514edbf34a9b91332923a3d8ec955ced0de426e4f2269503ee5ea", 20, 0},
    {"info.hint.next", "e58a30fd4c2f132dc7cb00df42838d8d2a06a5eb8635eb61348f2a91b04635ba", 18, 0},
    {"info.page", "a7c0b8328e8f9e71a6d164fb50b7915079030a4d0becd47fd1eeaafd00b65144", 5, 0},
    {"info.pages.10", "aef48b281a1a1221734432d84925580da9bccd62244571cdab6dac53477a3388", 8, 0},
    {"info.pages.9", "7323df20272345a3c299ef851dfcdd82b93845f59f3e5d62c677dfa9f2daca0f", 7, 0},
    {"info.pages.8", "1170fb3f9b1bab76c5478c42bfdab9593883a762de54b16dba143069420d6478", 7, 0},
    {"info.pages.7", "2991e046c7d61de13a7b3658ec64e0d57fd3898f2c60cce7fa691b12129cf108", 7, 0},
    {"info.pages.6", "9f1b26eea525378205b7999fb91e7c1ca3cf934dddeb55c201f718223afdaab2", 7, 0},
    {"info.pages.5", "4ff9c7d25432c9222fab0d7817210b79e62a36318f4d01fcd9a9a21a5babe0df", 7, 0},
    {"info.pages.4", "6f3c824e9ec38f7b38f0a3532826e840cbaa0893811e8a86e09c7195c21c4785", 7, 0},
    {"info.pages.3", "24607ac1e731a95d5053835efc5a7870ff9474a7e7a7ba05c783a015ea7df8c0", 7, 0},
    {"info.pages.2", "264c35a47f15983c19838c6812d551e1d2725f3b915df687ee32cb8b4394da57", 7, 0},
    {"info.pages.1", "b54457342c7d1e49081df00cc4ac29e76fcf2f98d229e849a6eed91e86891c9c", 7, 0},
    // END as3d hash list
};
const HashedText kAs2Hashes[] = {
    // BEGIN as2 hash list (generated from tools/exe_texts/as2.hashes.json)
    {"title.start_game", "62e0df134d7e80abb976d863e3602a9a240fe509ac17654521ac4ac6bffda44d", 10, 0},
    {"title.options", "08cbe41032eae11e08b067688e8314610448c76684aef5abfcec3f1889ff3d0c", 9, 0},
    {"title.controls", "b2b0b778fbab08540d02d00902e70778389a313aa001c2dafdc17b8291253dc0", 18, 0},
    {"title.heli", "543b0ab0c785855dff8c5956c3008a64c12a9536a7cb32667efb98bd844d4b57", 17, 0},
    {"title.mission_complete", "b557f61503eba00d07d71270f1982adca50cd8f11be28197423a0e774e021dc2", 16, 0},
    {"title.top_scores", "8ecbc9c2582d7e1cd69ec5d24d2f83906c6c555addf6f019d12161194c92e689", 10, 0},
    {"title.enter_name", "951ab65190399b2ee372d243b19ce78e2251feb5baed01d78f92431e027b9313", 15, 0},
    {"title.exit", "148b1405ef1550c0ad1563fccf2d93728b46a7b391b800678c068d898e39a1d6", 12, 0},
    {"title.hint", "42ce3b007c5aa0e3b1b3fdb2c722162e557782b5bb60022fa045b1c034ca2443", 12, 0},
    {"title.game_over", "71f10eb014cbdf8bd8d800a3e244226b92197ed0137371cddf77f5e18224b1d1", 9, 0},
    {"label.exit", "52f1e9abcb0fc0f937b72489c3b09c7d99d36a8f5a65a72f13d5300ba1a1b9a2", 30, 0},
    {"label.difficulty", "0554e44e82a9708d31aac6bb742519d23d518f667022c7b97718abd4f689b9cd", 11, 0},
    {"label.game_mode", "f72fbd1e95481e270974b860e5c0f19940d94c9da747aaa63d46062a544cc6cb", 10, 0},
    {"label.player", "8a1ad36da72e00858e3a19b07ee0eb6b48f4aba871d61448161aee240aa097ad", 7, 0},
    {"difficulty.0", "d83da69aa2c7147c3fc4480d3e812bbada3cb736c45a3b556cf271af2e725ff6", 9, 0},
    {"difficulty.1", "d6915875decb6e43dbf131d2453a2f065d78b915a3d0580777e834b9bf6afafd", 4, 0},
    {"difficulty.2", "a7248eeb45eb1ce247f81693283e22e8b39e8a50890cecd22d02e75025547d90", 6, 0},
    {"difficulty.3", "9e73f8fa9cb76c441b3118628a15b2bdf4116324954957961bacc171fbead4a3", 4, 0},
    {"difficulty.4", "864e8d565eac5de10aced470ce96018502bebf4262bbb9eb7e3eca8c0e045d10", 9, 0},
    {"mode.0", "441358b1b7a322d62548386fa92286ad195fa1e90104917b335c77d71d425efc", 13, 0},
    {"mode.1", "2ba147f4089632cec120beb25111785d3a30208c42a2ce9e4c41fdece1507da4", 11, 0},
    {"button.start_game", "5435e4c6684291a268aba45a9ba4d98586d4a0513091748012d1848c041788a5", 12, 0},
    {"button.top_scores", "135277e02a09be2abc2b1747e7096e7170db70784a8da25ad78e85e132c8cad4", 12, 0},
    {"button.options", "08cbe41032eae11e08b067688e8314610448c76684aef5abfcec3f1889ff3d0c", 9, 0},
    {"button.information", "2758b967a95f44262a64ea83f7409c7b2ab0790d2a185bb90a4f927f8becee0e", 13, 0},
    {"button.credits", "eb6eeef990b2d72a3e94ada22c184a19731fefe53e61d4fae56226425497c27f", 9, 0},
    {"button.quit", "028290bfbcd332272e4527cc67bdb420f17a3a8870e4b14a86adec3116bfa294", 6, 0},
    {"button.quit_wide", "4b0ffd532805ab88d65d0a6c05c68a26a855ce0270c7c85665850676790ba32d", 8, 0},
    {"button.yes", "9d57af6fee4c1809a058f77a1a22621e288a58231ed7bbb52790fd2c415c6372", 5, 0},
    {"button.no", "611d38bb65b9a9d5d675bc76a2f311be1768033ad0895e553aa9a57e21c55d91", 4, 0},
    {"button.back", "6c527a4a40f042f5be34bd103f19a33c6fbbc59b00a5644a32c6b9729a71ee1e", 6, 0},
    {"button.back_wide", "5e012ba8d0a8d5b2e5f39dada8f9f810fd9147ca70600c9d9aa8c0a87e3c402b", 8, 0},
    {"button.next", "a9100f98373c2e610224afd69100b1c4d2d382a9035b3a910c2f72d121490130", 6, 0},
    {"button.next_wide", "bcec728834d7c5e5167a64947c2f79150713b1ead64b37185cec34c01de327ce", 8, 0},
    {"button.start", "b909bfb672ddc8f3cae753d16b3f8e81f3dcbeafa75a0bba5f55ebab65787554", 7, 0},
    {"button.continue", "14759572db0df13062d353a7105753ee0d4a7b6c73eb8e5b573d040d4072b9ca", 10, 0},
    {"button.accept", "51c236045ef8ce0c9b6c90920d8f9841aefcf87decd50effb070187997099068", 8, 0},
    {"button.restart", "b97efbeb8d5e255fddf96dfdfe7a7c0dd72cb23d4b5d1857fa7eefcead14197f", 9, 0},
    {"button.resume", "f4346610ee331c1805ca1238bfb23011c2739b5541d7ac17da241fe4078dc912", 8, 0},
    {"button.choose_heli", "3e49ff06036d441628467983917919faccdbb3f6363de5307f4f803d5df61308", 19, 0},
    {"button.configure_controls", "34ef6f9b87cbc95b57d54ed91703150ffd31d51edac92123c9a27419d4b889dd", 20, 0},
    {"button.apply", "c18089a1019dbfdaac40e5e929639331921277e54cf41004945a8849b7d649da", 7, 0},
    {"button.ok", "28c520986465e024a417cd296174cbf4031112fae18979630e25f5273f8e4051", 6, 0},
    {"stat.enemies", "f15500002c5f0c0e8a5fcec320e3b719609ddc4abe4a890f2ba0afceba234c06", 18, 0},
    {"stat.stars", "e6ee92893d31f8d15494b698a79eae9ea6b8d8f9ac97f18ee931ca5983c95b89", 16, 0},
    {"stat.rank", "d6b5e3edd7dfe8835f479b133067375f3a7ceda09caae4ec1235c1d91fd2776b", 18, 0},
    {"msg.new_heli", "84b472810cda56c0e6bb3bc9364c0b43b36c3c40f409f821e63fdd9f0b89cc5a", 28, 0},
    {"rank.0", "3e15506b1ce2ee5e6c14e77df09c92476af59252bcac021cd9d029fd41125499", 7, 0},
    {"rank.1", "ea0bedb7c8b0f3cb364add8740dc5800838ff4aa0fe39458d1fbaab6afa00bd4", 6, 0},
    {"rank.2", "d8dc750ee2b2f28e16f02b090423299b3db49292e3b4b7f9c16551a64e4ce9e1", 12, 0},
    {"rank.3", "383e2d946278a872c3723cb83a961d8c4809df7110628cca0ba1a808267b3561", 5, 0},
    {"rank.4", "a93403d1eafa636fd871adc6aa56c36ca55642e2deee98a8d5e15906b3d1de58", 12, 0},
    {"rank.5", "143f40fdffa420df4a569d09f72cdb60bf2c2e5a6a8a0bd3cebb1effa8523177", 9, 0},
    {"rank.6", "1d6953e03ac29b9aba73dfcd57d8a5cb47991342495891f19b1cb713da89eb5d", 5, 0},
    {"heli.0", "7a99046b6efb9c3c9f240e4d1aa58192bd3b9ede21d3d05a2ef4bbb280890f5d", 11, 0},
    {"heli.1", "73a0ff1431ff1ee0e7539b1e12524783374bc36a09cb96dda8cacc9ff1689879", 10, 0},
    {"heli.2", "a32588efcb9d66e6c90112ce786eba3eb7f58a4e8826174fa3f6fb3d658e4076", 12, 0},
    {"heli.3", "f168e9939f05c09807038bc3f77459feae8ee442780e1455e59854421ce2c0d2", 14, 0},
    {"heli.4", "648f306cb1c1388188f75f2db3a45f94a9891821416fe689fd843aaee0bf0c49", 7, 0},
    {"heli.5", "2f1fe7a18b229e97b90babe77df33dc07e3029bc68a1ef30e7d111dc1c60409d", 11, 0},
    {"heli.speed", "6b23717a37a6321a966c86c1726f79a9936c80bc01441851e9c4d092e53ac274", 6, 0},
    {"heli.armor", "f40fc049bc82c215bfc543b86b28e25cc1f9a6a49ccb35626b3f1687b1f2ec50", 6, 0},
    {"heli.na", "3a7f3f0dfe0311badfa3c96c8168c236120d5f9eb1b0e9ee87116e4807f38948", 13, 0},
    {"scores.number", "334359b90efed75da5f0ada1d5e6b256f4a6bd0aee7eb39c0f90182a021ffc8b", 1, 0},
    {"scores.name", "dcd1d5223f73b3a965c07e3ff5dbee3eedcfedb806686a05b9b3868a2c3d6d50", 4, 0},
    {"scores.score", "38e5a46cbc5ad32842ac60a4f1425ac9b31c06bac685f446a06ab72b87de31ed", 5, 0},
    {"scores.rank", "a4130d7d2c3a137f362d28ba21eedd525b7e42e7551c3e62817b048c9a0bd425", 4, 0},
    {"opt.resolution", "7c144de0a9cc4a3b8a45fe0506933da39df43e71f816b70b8e49366b15848d8b", 11, 0},
    {"opt.refresh", "f09683b16c94e42096c67fbc38fae43749f14b22e5e5a1d20491d96e3cd1fa1f", 13, 0},
    {"opt.depth", "d4318d7ace8301cf1c0bb22d8cd85409fa27f7a00d59424fbbce4e1a8e352946", 12, 0},
    {"opt.fullscreen", "35b1ae4645741b7e8ccd70e0d376a126b5805cd132b4da7b83e57412088ee62a", 11, 0},
    {"opt.brightness", "7c8172d086b21c344785e708efba5b96e1b9de13e33d7c61ee162b357dbe33ca", 11, 0},
    {"opt.sfx", "0991660b6ce93d72e2540e6bef72dcaa54e4c0356fc1e24d64087137d50d5d1d", 13, 0},
    {"opt.music", "a4683bdab18046af1f13e817a818d3a137d248b45d8e2900950f396a1033a6a4", 13, 0},
    {"opt.sound3d", "e99c093216ae27f1b40be94a9c8b132c6398320f1befeb2f3391672c4a9aa427", 9, 0},
    {"opt.camera", "9185a900aa8d1263b0f5bff1d8f28363f5509d96c0b0371b651ed1df14a374e7", 7, 0},
    {"opt.mouse", "658ce7966ece901456a3bfbe822bf984475cf2f6f874a2c920c7e575bb4257e6", 14, 0},
    {"opt.refresh.default", "37a8eec1ce19687d132fe29051dca629d164e2c4958ba141d5f4133a33f0688f", 7, 0},
    {"opt.off", "ca7981b46ecf2c1787b6d76d81d9fd7fa0ca95842e2fcc2a452869891a9334d1", 3, 0},
    {"opt.on", "130011756125313c72f2cab730db4347e2841526d236a89cb25f38789fa49229", 2, 0},
    {"opt.depth.0", "21b111cbfe6e8fca2d181c43f53ad548b22e38aca955b9824706a504b0a07a2d", 7, 0},
    {"opt.depth.16", "15ea53482d7a1e6688225b8ae9c7142bde9e2cf5b00b98febd24d96539c35760", 6, 0},
    {"opt.depth.32", "4d5858ae0c310ca95f41d7637948d426ad3b8839b35f0676602c5979cef1b6ab", 6, 0},
    {"camera.0", "c3a191ff33e87a2a462da2520703f9e3643ff26f9af85b1d8cb5f39a96d5daf1", 9, 0},
    {"camera.1", "21b111cbfe6e8fca2d181c43f53ad548b22e38aca955b9824706a504b0a07a2d", 7, 0},
    {"camera.2", "c6b38344d3928e2162f9f22ad7d32a0b6e938a4803de2b7b1db124d730fcc929", 10, 0},
    {"camera.3", "739c21756c8f1002b37341a1c43383f795d5ae55df842140a11758d9f32f8a26", 8, 0},
    {"ctl.set", "9555d080d16d2575ff706111a50cc2876408b8ec4a6b3eef92b8b567e914eaa6", 13, 0},
    {"ctl.player.1", "27a99f01e114bf92a249aa3e2a3eb78731e143ca79f5f9ff38f1746f14f241f9", 8, 0},
    {"ctl.player.2", "4b67834a4c0fa5590d2e36e30f2e6904049935f543dbbfbaaa17ab7f47a4357d", 8, 0},
    {"ctl.row.0", "7699b56cc948a03f4fa01217314e0565f648af958cff1073008381e9c6fc3dc6", 14, 0},
    {"ctl.row.1", "f19b7f4e05f43178f2bd03224e06b4406fdf53b1f9207a88554f0cee9c37d440", 13, 0},
    {"ctl.row.2", "8b95ddcd7b3724467d4cbd44ad33c907ec8230cee10d35a2360e9b5bd3a93b93", 14, 0},
    {"ctl.row.3", "99e28e1f9ee82ace099b6e96ec08bf8cc280736a448aebc2925b99e55791e7f5", 15, 0},
    {"ctl.row.4", "2aca6670b3f0a5318c280ab4b2cea56c70d6cc244e05a14357c1e9c4ad436448", 8, 0},
    {"ctl.row.5", "de68fb991603c1420ec17060de521fc60ef1e974e1d95b484217e1c58b53da69", 11, 0},
    {"ctl.row.6", "9869fb3f1d0b18396199a15efe8947783059e947f901aa489168aa097ab99ffe", 12, 0},
    {"ctl.row.7", "b56cedbfb1e74dc7987cf5e47b2b1dc99d89eb50934a2f0a5e4c1fbf81f89a8b", 13, 0},
    {"ctl.row.8", "4f165b969549d5ec444ab2e482c5b9137327c0aa34b74ac2121635449acb9f24", 9, 0},
    {"ctl.row.9", "338f0879bcb2333ec5c3eed1e3b2b1640bf606b5b1f578a82c32e568769276d4", 10, 0},
    {"info.page", "a7c0b8328e8f9e71a6d164fb50b7915079030a4d0becd47fd1eeaafd00b65144", 5, 0},
    {"info.hint.prev", "99d9b5f5a15514edbf34a9b91332923a3d8ec955ced0de426e4f2269503ee5ea", 20, 0},
    {"info.hint.next", "e58a30fd4c2f132dc7cb00df42838d8d2a06a5eb8635eb61348f2a91b04635ba", 18, 0},
    {"info.pages.1", "7caa0434225a23407440acd2897c215e5d482fa63941aced5ed74bf95822b2e4", 6, 0},
    {"info.pages.2", "7cd315cc969ac521520d7c355e3715e85ac25adc17903318bc9bf74033e41912", 6, 0},
    {"info.pages.3", "11cbe252e5fae4ef405b59594016e926499f0cedd4a7fc9a3b5ae9e7622eb83b", 6, 0},
    {"info.pages.4", "e9dce302fe601dc650111f229113a3aae4fc8ae9f5870e2be74f27b6059a4aa6", 6, 0},
    {"info.pages.5", "7e7cd6cdde07e12fe8b2c6e2b41409e6526f30ff7b02a08d0ca9a8f64e1c4c45", 6, 0},
    {"info.pages.6", "e28867c0e814576ce24df13e3bfa2efda9c11ae3b2dceb63badb45fe4677b1cd", 6, 0},
    {"info.pages.7", "4904a80c79fb10eac645b75577932dbd6a76913b644a3106db895bdd0350c494", 6, 0},
    {"info.pages.8", "e38f88299bd6b2c659ceec0c39f8c4051eca4ffdf59157b26c92bdda9b4ff777", 6, 0},
    {"info.1.title", "709276c8392f940bfd855824583be68f48be48f22a303008784cc51aebc2c08f", 8, 0},
    {"info.1.0", "ea285ee753d3e832e7a6050c949811302b86f1e734160e8ce9a14ab4d967a0e6", 65, 0},
    {"info.1.1", "56ac180acdf5d42baa570cc1c40eff9458866b036e1b741920b4d6c10f2ecc4c", 63, 0},
    {"info.1.2", "3d9a8fbc568c449a1db4a87510e38962c002d5bd35b43ea6d8af162752ac85fe", 61, 0},
    {"info.1.3", "c8c73f3fb838babc995b4b9561e763402798b4a386cd8f2c41333a1d5c86eda1", 15, 0},
    {"info.1.5", "08757104af30e2cd70d3236d8e51c84c72221a3693e23589a4ef9f2f6301164d", 45, 0},
    {"info.1.6", "d48e98a15472f7c7ca9d40ebec072df71655a5e30121720994644fba8fbe27ef", 71, 0},
    {"info.1.8", "f0fe3fa39c4ab78da6b7994a19193c347c42ba4ed87691ab32bc544079ccc546", 66, 0},
    {"info.1.9", "41fe95f1f5d4d167439cf250d215e52da5f5f403f6d665dfa98ebb927375b832", 66, 0},
    {"info.1.10", "35133d61aabaa8605fd5e2ab7e9aeb1970bfc733c9c5e4ccf59de36a9993f78f", 66, 0},
    {"info.1.11", "221049039a1d8896f7a8d8fe4efe378efbb7fd4647722373293c3c490179c44b", 17, 0},
    {"info.2.title", "aa487c32011d30f244572b1bbd961eee238fa13a941d1b345adacbefa1f29a46", 29, 0},
    {"info.2.0", "e9f75b3efdb9aae85a6d7844d7a0b4246ad7694095078f285edacb64cbf7f93d", 13, 0},
    {"info.2.1", "28d6877a0e7498084b6cc00f1e065e4ec1b787cd8cf1a0b3f63c5e2bfeaca011", 57, 0},
    {"info.2.2", "485f1d8589320bbe15c60b4f0508ed781a3ad361591e0f66e08a3ba4d0ad061d", 58, 0},
    {"info.2.3", "108441af9b058980f53321dd045e9055f13015859fccc8d4c8f1361ba2fcf6c4", 26, 0},
    {"info.2.5", "940180ea9dae63794c24616acb7083afa6078e1ccf5b7515b9d27d69611a7fe9", 13, 0},
    {"info.2.6", "205c1f2be9f2e8c5360c18831dcbc4d3d3b6fea7791d1c652e77a3ebf7de61d7", 57, 0},
    {"info.2.7", "e32c48d3d2cde375fbd1b4b2715a64d3e2f760f05c97750aad6f16bf6d65802d", 17, 0},
    {"info.2.9", "66bc482924d34f836f8ba8cf99d3c15cd0a203a926d825a08ff3eb7d465d680a", 15, 0},
    {"info.2.10", "35a3638e63c520dc3c5e6021d3da53659e3d412f3faaacff1f21d81359d4a07f", 49, 0},
    {"info.2.11", "66a98da5e11153029bcbebd740f709cdce3b3b0e9436c36eb15b995bfad6bca8", 58, 0},
    {"info.2.13", "ce580082666ebb3feab080cc28835d1e89858ff5fa141875dee471a59c79da08", 13, 0},
    {"info.2.14", "3fc46d90bf66060bc474852720cd4015a90f9753abbecbdcf1eb88ac15233b72", 55, 0},
    {"info.2.15", "13a21272f47cebf758829c9341dc087cafe51e0620da2dd39072e0c94e668c0a", 23, 0},
    {"info.3.title", "d7a7fe75311118fd386fbcf9a7a86149408b0abfa15130ddc2294fdaf33ecc56", 29, 0},
    {"info.3.0", "02fc2f1cc8bdbf7f08a1973fa3de1439138204b3eff288e3410baa440624f69d", 11, 0},
    {"info.3.1", "8a22e5dffdff77beabdd3103e07eacb3301cbaa1b8e215bb714c65bdb7017327", 61, 0},
    {"info.3.2", "896b12ad6ecb9c4d06b5c022d9f0f199547999b42ad3f1f75badacf886a21b98", 56, 0},
    {"info.3.3", "78bfa514bb1d62acaeab49e37c7755c636eb3b661e6e92aa22e32524679c1671", 55, 0},
    {"info.3.4", "e721e452eff0808c6ac6f6d77ff21b8ab0f73136bbc37f4f3e1e510d31a69685", 28, 0},
    {"info.3.6", "1fa400010cce4b60561b362ce5f7f4d2fbbe73db2b04246a306f48b9b0d7b48d", 15, 0},
    {"info.3.7", "d726aaafa0253a24494344182d99fe95d42e5e6bb236f0b9daf864c1717d7a1f", 57, 0},
    {"info.3.8", "87cf81fdd2f56ff5abdf53814bae24ce3dc2bfe9b81d47d9356e9896bf40cf4e", 45, 0},
    {"info.3.10", "a67da37b00808c479ae85d40680b31aa3faa6cfb607ea14094d1f2dc1debbb85", 10, 0},
    {"info.3.11", "32de08f5670a18d703a3db7baa52bfd5416d5ff63ce413fc3ff0b6ecd768c2d8", 47, 0},
    {"info.3.12", "bc7b068e2463e4ce76b55d212239cd9313ee1551d71348432cd6b60a97499a35", 16, 0},
    {"info.4.title", "3c75037a1bd2285262650c3780a71756553c0db18e5c77b188949dff12562942", 29, 0},
    {"info.4.0", "bb1a1eb3ede03e2a4196f3611beb9e7238ece5e5198dac72d13677a301d6b1a8", 13, 0},
    {"info.4.1", "201baebb59ac5eff0dd263071b624ff5bce1701132f3b2e1928b71f07ea01ad8", 57, 0},
    {"info.4.2", "9d7bc23dd8ecc8418bdc63008ccadeb8c69269c482164bec4deab33b16498ecf", 51, 0},
    {"info.4.4", "0f6cc450027675b55b965dd96e43ea46206014bb0b9ae140c3b47271cf8073a5", 14, 0},
    {"info.4.5", "99e010bcf4b1a3ceaf8c0c601ed335fddc6b1edf3e9688dedd0f6320e335fba9", 17, 0},
    {"info.5.title", "3bc9c521b514763db1982ee4b5aaa712cf8c77c94a85eb4a22cae9aefb2183cd", 22, 0},
    {"info.5.0", "bade0e396be81863cdd900fe9ec70e666ac2ea88f04027c06115601d95717c74", 16, 0},
    {"info.5.1", "5339212530f36a34bb534c5be9515b5889398393fe95da0b175a6288ff08be7e", 58, 0},
    {"info.5.2", "1a97be088ae53fd404e3f055a6654d415f180ad0e9841b81a8b56799bdd2f122", 60, 0},
    {"info.5.4", "a770689b6125453b05b771b1d6d5797070cfb904760b704b0b8e8022499d7afd", 14, 0},
    {"info.5.5", "e762b3679bab00281936c50fa310316356b87c9bcd0ed965b0467b6498724419", 53, 0},
    {"info.5.7", "88ee541402d0e65d8afc3f4ede2cb1e0d9dea94424fe99719e8f10ab29345c74", 29, 0},
    {"info.5.8", "c34225f08b1e102f222ca8883def1402b33b44cece899ffc4d548695c58bff1b", 60, 0},
    {"info.5.9", "cc38a7acb0709496c6799e1af626c44e6c7c63eddcb03f58b5134fcc3c02093b", 57, 0},
    {"info.5.10", "a85faa09e43d645115cbd4d2267d51d2b8954e117c7cf683d03726f1f3c75238", 7, 0},
    {"info.5.12", "d7fd51fae0c6a21f9d5e3f8d37f15058bb7d8fd0021dece553bc92ad9c037ff5", 27, 0},
    {"info.5.13", "3fd100e58fab04e0bc128408ef3f211bbef6cbc816807556efd50b95ae540ef1", 59, 0},
    {"info.5.14", "c88ee7f14db815d2ace7a570a48989715d4a0c921df527310b2f1991708a0368", 54, 0},
    {"info.5.15", "2e42fb8b3e0da71eea57dae17df4820ac6f83c536eab159207ec71df70b6fe12", 40, 0},
    {"info.6.title", "9e469dbdcbe64d0de8dd2d12317bc42c6750d15c1987cde5e3668502d3a0a522", 22, 0},
    {"info.6.0", "d299498dd3952632bdbf0e5c609afef0b32241ac48680c21cb036dc52a9e723e", 17, 0},
    {"info.6.1", "c5242450f016f4b72b146899baaea921c2dc37f84aad2bde7bb7f202b3ba9f32", 55, 0},
    {"info.6.2", "424e95bdd9b2b365664fc2d739d9dfeb0d44d38a5e89248a73df40185f5ca5b1", 51, 0},
    {"info.7.title", "c0b05ce018d274f937bcff98340681970e186193a82feb450eda6464e50f9192", 19, 0},
    {"info.7.0", "df541ddcf7e7753e5c65aefb3ce9d2262557c5a05d5d17e4d6197924a0f6a522", 13, 0},
    {"info.7.1", "93fb821cfd908df6610f2d0b3c6e7da4af739df6a4ff834308cf502415fedb58", 56, 0},
    {"info.7.2", "76dd4310fb2fbc3768c90a612bf08fe1e4710b4292b76749354c92f64ab0d564", 41, 0},
    {"info.7.4", "e0603a89ecb713fc40b4193412294aca22f3f3d8b8253cef345ca83a6c5a7dbc", 14, 0},
    {"info.7.5", "304f3075513d947d6b1aa2a94258e2c779e1639e9e62aedbdc80bde6a47ad4e9", 40, 0},
    {"info.7.7", "22c49d44a0299584a24a11c86a020aa0c43418f495e3cb56e6517caebd4e75d6", 15, 0},
    {"info.7.8", "b7446d24a504b94a62804b9f49fcedf0e565737c71f26c2567a0928ca3b63e8a", 56, 0},
    {"info.7.10", "1de03c7866277d05acff9f84af65d7ebb8899c4ed2a011f1e63df87296d24eb0", 16, 0},
    {"info.7.11", "38a119c87c51bec6985bcff7b2cd493c4514cc656b1ca0789f941d48ede15c06", 53, 0},
    {"info.7.12", "2d580054c9c057fa52e1f5f8b5f20cd0f99ff10e0878f69bbc1e265a0cef21df", 55, 0},
    {"info.7.13", "439e00410757f3c00dc7e2d05831ef022ca5d6d9bff8508eac59417889306ac9", 39, 0},
    {"info.8.title", "64b16c12e8201e80d820bed07fb74d5328f88fc3bbd2b6148903acda59f9357e", 19, 0},
    {"info.8.0", "f995032be78d5a7ce9ecfa36b2751dc14cecfe19953f421c30268359e173d53f", 18, 0},
    {"info.8.1", "c6af8e22eac512aab334c05efdf03f81063e6681dacbb2d751e577dbf4744c27", 48, 0},
    {"info.8.2", "13dc681617532fa137ff977d97d802ad0b77ef865dbe4601f9628bc450b96891", 46, 0},
    {"info.8.4", "c5180907847857e8314eddfe40206b3f47da013582c76cf591a6c26b7141f858", 13, 0},
    {"info.8.5", "efd2108422c54cc3a864c52a4e24c5fa40b7479d9d42e18680d50633b728b6bb", 43, 0},
    {"info.8.6", "ea421426bc2199d048842d9b4fb4766bd81af3082216647dc306fe9e87d310cc", 32, 0},
    {"credits.0", "f98f234f4d4856b25aca9c07491542c02d74483587017dff522eba9b0aceaa1d", 14, 0},
    {"credits.1", "287e8d1d2e705e047dbd8df6591b1923e2f0125b722b910f779f3a83801bab84", 17, 0},
    {"credits.3", "7813d8075fd6bab9c439bf9c92fcb1321870d4425515ce365ccc7237fd01cf1a", 11, 0},
    {"credits.4", "4ff8f00a35a38c3352764d4688cbb57d133908c4b5ddfc9a6e68ea49587ff109", 14, 0},
    {"credits.6", "6e6642d1311dde74c063e66cc03fb37a4acf9b1ea51940504bc1f4e53cb2b488", 26, 0},
    {"credits.7", "6aba6815d938beed82c2992a734beecb9a24b91d830c70cd05bd318cfbb0ad80", 15, 0},
    {"credits.8", "a2d6bb95a41879bec2db518d14762e8d5ba5b70e4ac9659c03c58c25afa96c37", 18, 0},
    {"credits.10", "9f7e01b9cd9753b86df08cfad8a29611321ad398d6c73310065ed4f1550adfee", 18, 0},
    {"credits.11", "21f5fe273aebb9c64e9b0a5c181e2689415dc9c2d022870228d3b030b1ed17da", 14, 0},
    {"credits.13", "dd25ca373419891465550e345700016ee9f13112e28407a0ed4c0d9c6c10e02a", 14, 0},
    {"credits.14", "87438dcb08ff27cb3cc1f0ccc7ff7d073beeee5445fb898423996326c2d5d35a", 16, 0},
    {"credits.16", "08d5f92ca9e9cecc2807e89018f28f8d705476ab927b7c4ca79bda0efbe4baab", 5, 0},
    {"credits.17", "2d037dfefc2c77d60c8277aebb9284c49cf122e68b9c58c0bcbcecdba2ed84eb", 24, 0},
    {"credits.19", "ac8fca60214a65f7c4894650b7663fa5a4d8b33d28763590a15c3d93fc2ebe2a", 13, 0},
    {"credits.20", "06033d4f502783cdece2e70e390b7dc603cdf222af19c8174abe0fec68c83f81", 20, 0},
    {"congrats.0", "6a9fac33b7c1e18c95bd33f3daeeb5dc4e9589257ad2ea8e1a6ee2bf2a9bb181", 16, 0},
    {"congrats.2", "7d7d0c6c827dbc59354bd99eb64ca72320b3d42da656b0cba05e0398e031c653", 57, 0},
    {"congrats.3", "9fe171f8440a27ad6bcb7ab93eeccb3b1dfca1ea4067a7aabddb92e8181f3620", 57, 0},
    {"congrats.4", "f5179b19c573b1a2f1957c1580b229edba9e53e8bbc78d1b670efb7076dabe68", 44, 0},
    {"congrats.6", "aded2152f22ec553f53d98518f9fc25904dec5139144404ad1cc3dd621760294", 64, 0},
    {"congrats.7", "f41ee8bae3205fd06366a78b69334cf6700f6359cc276c7e65ea2606face39af", 56, 0},
    {"congrats.8", "2d78ae29fe2124e8bfa81a2750f4b67cf25ed75d5607efa3ca335b68225fb6d6", 22, 0},
    {"congrats.10", "7011afa8d803afa67ccf212e18ca273d515c514d5d15b1cca4d48ae94757b5ec", 20, 0},
    {"loading.label", "dc380888c4e2c7762212480ff86eb39150ec70b45009c33bc6adcbd0041384b1", 7, 0},
    {"cheat.god_on", "3a77a0a8509a47fcdf52e88b4930d61d56df21fa23aa972de34a3d6909126d67", 17, 0},
    {"cheat.god_off", "8f0b1c75c77f8cd32a4a57d75692633ccf4f8a35ad6c1105ffda208d93e39227", 18, 0},
    {"cheat.lives", "3a035de717b5b7ae68f6eb2b39430a8d0cd08e7c559d47611f16c0613f46da29", 18, 0},
    {"cheat.weapons", "f1252ab4122cdd5678972d07fc4f4a2803de9ccb559bdf7d86862f0b2c983a75", 20, 0},
    {"cheat.missiles", "c9da398127e39125129538e8d63b65c700186df89c2f6da654652135d2b56f5f", 21, 0},
    {"cheat.powerups", "beaa0bbe1c9175c8873743ea78bd2bab45ad8fb0d6136dcd4bd467fdf2554943", 22, 0},
    {"dialog.1.start.0", "4236e9df3a7655fa0975fecf615af0d3e0aeefe903e1262034f20fc394811e60", 167, 0},
    {"dialog.1.start.0.speaker", "", 0, 0u},
    {"dialog.1.start.1", "0c3ea0bc6c56d9c4c278db7718eda3a9d2ffecd9a42473cc9d56dd004055a14d", 62, 0},
    {"dialog.1.start.1.speaker", "", 0, 1u},
    {"dialog.1.end.0", "518abe37dd0e1dba492d18d8ba89e8c468510fb4c43357145ce7ea2c124f254d", 46, 0},
    {"dialog.1.end.0.speaker", "", 0, 0u},
    {"dialog.2.start.0", "e5336f94500d2a0ddf22235e0e42bfbd2618ef160bde25902296c4a60665e99e", 122, 0},
    {"dialog.2.start.0.speaker", "", 0, 0u},
    {"dialog.2.start.1", "de8ae77616620ebdee77c2f25759fd43414cacf8460021ba10da0118ac52d90d", 10, 0},
    {"dialog.2.start.1.speaker", "", 0, 1u},
    {"dialog.2.end.0", "70a989828419cf5dd07cfccf99f42d29b2f71da89472c7d9ed998507dfe4c286", 126, 0},
    {"dialog.2.end.0.speaker", "", 0, 1u},
    {"dialog.3.start.0", "43e81c3b44cca80960d08d8f21c2695b1e0108258e38ef1c82560e7b0f763d6a", 129, 0},
    {"dialog.3.start.0.speaker", "", 0, 1u},
    {"dialog.3.start.1", "a04125163db4aa27df6bb186c629fbe6d6be6743825ca18a5d08755fd6286a6b", 107, 0},
    {"dialog.3.start.1.speaker", "", 0, 0u},
    {"dialog.3.start.2", "85216719c9f3931f8ab83ba922fa0c215c0b118d85f324fe77c861ee327535aa", 121, 0},
    {"dialog.3.start.2.speaker", "", 0, 0u},
    {"dialog.3.end.0", "780f035c8e484866cfc38f51289cdd7cc94cf3530f1fce4b91862a9ab9ecfd7a", 26, 0},
    {"dialog.3.end.0.speaker", "", 0, 1u},
    {"dialog.4.end.0", "50bc133e05434024d244f91dc3ff09a510232dec0c06f22b73d599cace2e28b0", 9, 0},
    {"dialog.4.end.0.speaker", "", 0, 0u},
    {"dialog.5.start.0", "1ed1b53a909dc884ca1f93020d7fbab18055e0343e747bcdd695c2105cb76e37", 145, 0},
    {"dialog.5.start.0.speaker", "", 0, 0u},
    {"dialog.5.end.0", "d64e2ec26cf1c7299836f3a5af27b0c0f48c7a34f866e1919993d02efd5be8bf", 75, 0},
    {"dialog.5.end.0.speaker", "", 0, 1u},
    {"dialog.6.start.0", "770696a2eda70befaa3be31dc174303a89685a82b31c35a7cdd4e9611698f66d", 124, 0},
    {"dialog.6.start.0.speaker", "", 0, 1u},
    {"dialog.6.start.1", "1981d7093573b8cc57b13868d959c597fd81b51eba7530bd8cc0858ac9d3c279", 101, 0},
    {"dialog.6.start.1.speaker", "", 0, 0u},
    {"dialog.6.end.0", "5ba9038c1864af351bcec08c622ed7786efaf22639ded905050de0e95a59a87e", 60, 0},
    {"dialog.6.end.0.speaker", "", 0, 1u},
    {"dialog.6.end.1", "c45324dfec2330b8142260ca2e29087aac51bb097834208c2923737bcf6028b0", 35, 0},
    {"dialog.6.end.1.speaker", "", 0, 0u},
    {"dialog.8.start.0", "6ba31256b3a77ef60b084cff228833f2a4a63994a11eef7f0fbd4dea6bbdd8e1", 120, 0},
    {"dialog.8.start.0.speaker", "", 0, 0u},
    {"dialog.8.start.1", "1a8d657dd0cf5a26b0fc84b3f3f00102cf1a9a193be23b45a61ca37041bdba9d", 167, 0},
    {"dialog.8.start.1.speaker", "", 0, 1u},
    {"dialog.8.start.2", "c8f22081fd223f1fe4348fe668c877053364388473f630e070420a31f5bda245", 50, 0},
    {"dialog.8.start.2.speaker", "", 0, 0u},
    {"dialog.9.end.0", "84a4512e17af0965d9eaaa659bd01badeff6fbecbe7866dfa3c108c38549308c", 21, 0},
    {"dialog.9.end.0.speaker", "", 0, 0u},
    {"dialog.10.end.0", "f667fd90bf4572852c80c84e05e76bef2eeecb71951902214f12107db8d766bf", 39, 0},
    {"dialog.10.end.0.speaker", "", 0, 0u},
    {"dialog.10.end.1", "260bb0b326a9917c9a1473601ab972be6b1f272790988aab57a2f55c6354ff02", 78, 0},
    {"dialog.10.end.1.speaker", "", 0, 1u},
    {"dialog.11.start.0", "a80881b902da2194d9da0e087a2384302833613bb605a6d1036fe9f1158e0015", 167, 0},
    {"dialog.11.start.0.speaker", "", 0, 0u},
    {"dialog.11.start.1", "b711af65e91cf2a29f7b4f609ec807f03853c6bcb5eed4d3bfd0fae04102f392", 25, 0},
    {"dialog.11.start.1.speaker", "", 0, 1u},
    {"dialog.11.end.0", "5c02d3fe83778615e75b4ad0a8367e672daebf90deeb671ce9e47eb85b25ccf0", 87, 0},
    {"dialog.11.end.0.speaker", "", 0, 1u},
    {"dialog.12.start.0", "3950ab4183557c699609299f5bbcfcb04ce1d071d70fa6839ca2c6b761659d79", 164, 0},
    {"dialog.12.start.0.speaker", "", 0, 0u},
    {"dialog.12.end.0", "1cf1857ee35672e4f415b638d6692e09f665af2b58d64333330de607ffbf028b", 25, 0},
    {"dialog.12.end.0.speaker", "", 0, 1u},
    {"dialog.12.end.1", "cdfa47289aabe7a1417bc27b952f42adb924be29136c9dbc9642ca4c82f537fd", 42, 0},
    {"dialog.12.end.1.speaker", "", 0, 0u},
    {"dialog.14.start.0", "ed74b90c839141637976b53dbbe98f210f596470ce23620e3b99d7208c74fceb", 64, 0},
    {"dialog.14.start.0.speaker", "", 0, 1u},
    {"dialog.14.start.1", "78e2d187c426474fc60db4908d0292a5ec10199d19ccee488a1728f79337eaa5", 36, 0},
    {"dialog.14.start.1.speaker", "", 0, 0u},
    {"dialog.15.start.0", "7c6e46806e8e6ba9a4aee1012f732566dd6eae1807da0c926c4e3943e49abaff", 79, 0},
    {"dialog.15.start.0.speaker", "", 0, 0u},
    {"dialog.15.start.1", "aca2a322337d288aa5135518bf338737faa56aa1249c327a4f9b644dfbe69c40", 7, 0},
    {"dialog.15.start.1.speaker", "", 0, 1u},
    {"dialog.15.end.0", "50e79467fe97a2aef84c8cb294961363fc399b225f52fee2694cfbf998b77e3d", 113, 0},
    {"dialog.15.end.0.speaker", "", 0, 1u},
    {"dialog.16.end.0", "853d61ec63f618efbad150b9801c787fd6b3a78f45a0b9eb57e2b2922654c691", 33, 0},
    {"dialog.16.end.0.speaker", "", 0, 0u},
    {"dialog.17.start.0", "5c87b7771e8c8725778e2fdcdfc4a29050e0ca2bd0d412b164dce02ae74cf4a9", 70, 0},
    {"dialog.17.start.0.speaker", "", 0, 1u},
    {"dialog.17.start.1", "e82389860dea92c7b05c063890988f9a20ccd680e7fd43832d1896351642addc", 62, 0},
    {"dialog.17.start.1.speaker", "", 0, 0u},
    {"dialog.18.start.0", "34fcd77f547277f62ae11fffa8b962f4b7f05abd4181800511136fb9eff206e2", 138, 0},
    {"dialog.18.start.0.speaker", "", 0, 0u},
    {"dialog.18.start.1", "dcfcd281efebe2891be8137a8618dd52fd8eb9b0a2076c66e12e590e95fc4809", 90, 0},
    {"dialog.18.start.1.speaker", "", 0, 1u},
    {"dialog.18.start.2", "70c832ec0a932deecbe583f76effe05f1eee140c26dd609e8e224b82d209fcc8", 57, 0},
    {"dialog.18.start.2.speaker", "", 0, 0u},
    // END as2 hash list
};
const HashedText kGulfHashes[] = {
    // BEGIN gulf hash list (generated from tools/exe_texts/gulf.hashes.json)
    {"title.start_game", "62e0df134d7e80abb976d863e3602a9a240fe509ac17654521ac4ac6bffda44d", 10, 0},
    {"title.options", "d0db8b5e364b6989e4b8f38a2b3846b87d99a51c1592ebf35d21cc66405a8e39", 7, 0},
    {"title.controls", "b2b0b778fbab08540d02d00902e70778389a313aa001c2dafdc17b8291253dc0", 18, 0},
    {"title.heli", "543b0ab0c785855dff8c5956c3008a64c12a9536a7cb32667efb98bd844d4b57", 17, 0},
    {"title.mission_complete", "b557f61503eba00d07d71270f1982adca50cd8f11be28197423a0e774e021dc2", 16, 0},
    {"title.top_scores", "8ecbc9c2582d7e1cd69ec5d24d2f83906c6c555addf6f019d12161194c92e689", 10, 0},
    {"title.enter_name", "951ab65190399b2ee372d243b19ce78e2251feb5baed01d78f92431e027b9313", 15, 0},
    {"title.exit", "148b1405ef1550c0ad1563fccf2d93728b46a7b391b800678c068d898e39a1d6", 12, 0},
    {"title.hint", "42ce3b007c5aa0e3b1b3fdb2c722162e557782b5bb60022fa045b1c034ca2443", 12, 0},
    {"title.game_over", "71f10eb014cbdf8bd8d800a3e244226b92197ed0137371cddf77f5e18224b1d1", 9, 0},
    {"label.exit", "52f1e9abcb0fc0f937b72489c3b09c7d99d36a8f5a65a72f13d5300ba1a1b9a2", 30, 0},
    {"label.difficulty", "0554e44e82a9708d31aac6bb742519d23d518f667022c7b97718abd4f689b9cd", 11, 0},
    {"label.game_mode", "f72fbd1e95481e270974b860e5c0f19940d94c9da747aaa63d46062a544cc6cb", 10, 0},
    {"label.player", "8a1ad36da72e00858e3a19b07ee0eb6b48f4aba871d61448161aee240aa097ad", 7, 0},
    {"difficulty.0", "d83da69aa2c7147c3fc4480d3e812bbada3cb736c45a3b556cf271af2e725ff6", 9, 0},
    {"difficulty.1", "d6915875decb6e43dbf131d2453a2f065d78b915a3d0580777e834b9bf6afafd", 4, 0},
    {"difficulty.2", "a7248eeb45eb1ce247f81693283e22e8b39e8a50890cecd22d02e75025547d90", 6, 0},
    {"difficulty.3", "9e73f8fa9cb76c441b3118628a15b2bdf4116324954957961bacc171fbead4a3", 4, 0},
    {"difficulty.4", "864e8d565eac5de10aced470ce96018502bebf4262bbb9eb7e3eca8c0e045d10", 9, 0},
    {"mode.0", "441358b1b7a322d62548386fa92286ad195fa1e90104917b335c77d71d425efc", 13, 0},
    {"mode.1", "2ba147f4089632cec120beb25111785d3a30208c42a2ce9e4c41fdece1507da4", 11, 0},
    {"button.start_game", "62e0df134d7e80abb976d863e3602a9a240fe509ac17654521ac4ac6bffda44d", 10, 0},
    {"button.top_scores", "8ecbc9c2582d7e1cd69ec5d24d2f83906c6c555addf6f019d12161194c92e689", 10, 0},
    {"button.options", "d0db8b5e364b6989e4b8f38a2b3846b87d99a51c1592ebf35d21cc66405a8e39", 7, 0},
    {"button.information", "1cb0ba125f84c982b00ba3639cdfb2cee6b70f94b5e0115c045fef607af4f216", 11, 0},
    {"button.credits", "2a6b24ad287220345e96eb8021fe29d42b0785766c8df658827e7251da2d36dc", 7, 0},
    {"button.quit", "b3eb37acb66ba4bbe8fb1ba296815daf194be94f5a5fb50dab2fb5e4a57ba29e", 4, 0},
    {"button.quit_wide", "9d5912c711f17d2489c6b46db734e9a341bd9ae005e2bf01dcd2ad2d1e95620e", 10, 0},
    {"button.yes", "9d57af6fee4c1809a058f77a1a22621e288a58231ed7bbb52790fd2c415c6372", 5, 0},
    {"button.no", "611d38bb65b9a9d5d675bc76a2f311be1768033ad0895e553aa9a57e21c55d91", 4, 0},
    {"button.back", "7d6e31af72c35b2a1e2fa2007e32a8fb0672fddbacf09a8d901b662a46244402", 10, 0},
    {"button.back_wide", "7d6e31af72c35b2a1e2fa2007e32a8fb0672fddbacf09a8d901b662a46244402", 10, 0},
    {"button.next", "ebea575dec2fded8574660a6dec5d70b66d027f3cb454051c1a5dcb1bb0f797a", 10, 0},
    {"button.next_wide", "ebea575dec2fded8574660a6dec5d70b66d027f3cb454051c1a5dcb1bb0f797a", 10, 0},
    {"button.start", "9dec7bfd94fac375c84a6cd1f0de3263c08512d26caed942b074465e9c3ce298", 9, 0},
    {"button.continue", "7d329b5b50c20757ab949452596b3944a5f7d01a47f611872c107af30fe2f37c", 12, 0},
    {"button.continue.heli", "14759572db0df13062d353a7105753ee0d4a7b6c73eb8e5b573d040d4072b9ca", 10, 0},
    {"button.accept", "e0cc9aa57149a9890d4b25d6d758fe48f9a6186ee727c47cd0c94c8aff7a60dd", 10, 0},
    {"button.restart", "ead65ace0a6cd6573d70cf20f2c528e04c91e592065af8aaed86e85fb5097ee8", 13, 0},
    {"button.restart.gameover", "2b222cee269ff27bb957d89505d54cc473fbd6fb8c9ecffdc8150339d82fb8e0", 11, 0},
    {"button.resume", "d640c7421da066618a39daf4266da5f61f66a9b1afcd626481b831a17064965d", 6, 0},
    {"button.choose_heli", "9956e2949bfdccaede2afaac16315448b1819dc6000018e84902174fd4a5c8bd", 21, 0},
    {"button.configure_controls", "bc29286ca86b5371656a14e28923e56f27e724519da74a691dbb1dca08cd8dae", 22, 0},
    {"button.apply", "8728521935a9e2e1ce9d906811d7d2ea8cf39783c8ca7429c01d443c92dd12f8", 11, 0},
    {"button.ok", "bd18c8d8d4be6d4859b5256a76bcc6f59a232f6720bd79f2f2bb8b711fb4a877", 8, 0},
    {"button.hint_ok", "e589932b4b3e05352cb28401b805f34031308997d285a01157e7a60966cbb537", 10, 0},
    {"stat.enemies", "f15500002c5f0c0e8a5fcec320e3b719609ddc4abe4a890f2ba0afceba234c06", 18, 0},
    {"stat.stars", "e6ee92893d31f8d15494b698a79eae9ea6b8d8f9ac97f18ee931ca5983c95b89", 16, 0},
    {"stat.rank", "d6b5e3edd7dfe8835f479b133067375f3a7ceda09caae4ec1235c1d91fd2776b", 18, 0},
    {"msg.new_heli", "84b472810cda56c0e6bb3bc9364c0b43b36c3c40f409f821e63fdd9f0b89cc5a", 28, 0},
    {"rank.0", "3e15506b1ce2ee5e6c14e77df09c92476af59252bcac021cd9d029fd41125499", 7, 0},
    {"rank.1", "ea0bedb7c8b0f3cb364add8740dc5800838ff4aa0fe39458d1fbaab6afa00bd4", 6, 0},
    {"rank.2", "d8dc750ee2b2f28e16f02b090423299b3db49292e3b4b7f9c16551a64e4ce9e1", 12, 0},
    {"rank.3", "383e2d946278a872c3723cb83a961d8c4809df7110628cca0ba1a808267b3561", 5, 0},
    {"rank.4", "a93403d1eafa636fd871adc6aa56c36ca55642e2deee98a8d5e15906b3d1de58", 12, 0},
    {"rank.5", "143f40fdffa420df4a569d09f72cdb60bf2c2e5a6a8a0bd3cebb1effa8523177", 9, 0},
    {"rank.6", "1d6953e03ac29b9aba73dfcd57d8a5cb47991342495891f19b1cb713da89eb5d", 5, 0},
    {"heli.0", "a816232cf6a2d4de96eede3634d9a693c2e0652035bf2f43f8b4d42d78c4de82", 8, 0},
    {"heli.1", "73a0ff1431ff1ee0e7539b1e12524783374bc36a09cb96dda8cacc9ff1689879", 10, 0},
    {"heli.2", "a32588efcb9d66e6c90112ce786eba3eb7f58a4e8826174fa3f6fb3d658e4076", 12, 0},
    {"heli.speed", "6b23717a37a6321a966c86c1726f79a9936c80bc01441851e9c4d092e53ac274", 6, 0},
    {"heli.armor", "f40fc049bc82c215bfc543b86b28e25cc1f9a6a49ccb35626b3f1687b1f2ec50", 6, 0},
    {"heli.na", "3a7f3f0dfe0311badfa3c96c8168c236120d5f9eb1b0e9ee87116e4807f38948", 13, 0},
    {"scores.number", "334359b90efed75da5f0ada1d5e6b256f4a6bd0aee7eb39c0f90182a021ffc8b", 1, 0},
    {"scores.name", "dcd1d5223f73b3a965c07e3ff5dbee3eedcfedb806686a05b9b3868a2c3d6d50", 4, 0},
    {"scores.score", "38e5a46cbc5ad32842ac60a4f1425ac9b31c06bac685f446a06ab72b87de31ed", 5, 0},
    {"scores.rank", "a4130d7d2c3a137f362d28ba21eedd525b7e42e7551c3e62817b048c9a0bd425", 4, 0},
    {"opt.resolution", "7c144de0a9cc4a3b8a45fe0506933da39df43e71f816b70b8e49366b15848d8b", 11, 0},
    {"opt.refresh", "f09683b16c94e42096c67fbc38fae43749f14b22e5e5a1d20491d96e3cd1fa1f", 13, 0},
    {"opt.depth", "d4318d7ace8301cf1c0bb22d8cd85409fa27f7a00d59424fbbce4e1a8e352946", 12, 0},
    {"opt.fullscreen", "35b1ae4645741b7e8ccd70e0d376a126b5805cd132b4da7b83e57412088ee62a", 11, 0},
    {"opt.brightness", "7c8172d086b21c344785e708efba5b96e1b9de13e33d7c61ee162b357dbe33ca", 11, 0},
    {"opt.sfx", "0991660b6ce93d72e2540e6bef72dcaa54e4c0356fc1e24d64087137d50d5d1d", 13, 0},
    {"opt.music", "a4683bdab18046af1f13e817a818d3a137d248b45d8e2900950f396a1033a6a4", 13, 0},
    {"opt.sound3d", "e99c093216ae27f1b40be94a9c8b132c6398320f1befeb2f3391672c4a9aa427", 9, 0},
    {"opt.camera", "9185a900aa8d1263b0f5bff1d8f28363f5509d96c0b0371b651ed1df14a374e7", 7, 0},
    {"opt.mouse", "658ce7966ece901456a3bfbe822bf984475cf2f6f874a2c920c7e575bb4257e6", 14, 0},
    {"opt.refresh.default", "37a8eec1ce19687d132fe29051dca629d164e2c4958ba141d5f4133a33f0688f", 7, 0},
    {"opt.off", "ca7981b46ecf2c1787b6d76d81d9fd7fa0ca95842e2fcc2a452869891a9334d1", 3, 0},
    {"opt.on", "130011756125313c72f2cab730db4347e2841526d236a89cb25f38789fa49229", 2, 0},
    {"opt.depth.0", "21b111cbfe6e8fca2d181c43f53ad548b22e38aca955b9824706a504b0a07a2d", 7, 0},
    {"opt.depth.16", "15ea53482d7a1e6688225b8ae9c7142bde9e2cf5b00b98febd24d96539c35760", 6, 0},
    {"opt.depth.32", "4d5858ae0c310ca95f41d7637948d426ad3b8839b35f0676602c5979cef1b6ab", 6, 0},
    {"camera.0", "c3a191ff33e87a2a462da2520703f9e3643ff26f9af85b1d8cb5f39a96d5daf1", 9, 0},
    {"camera.1", "21b111cbfe6e8fca2d181c43f53ad548b22e38aca955b9824706a504b0a07a2d", 7, 0},
    {"camera.2", "c6b38344d3928e2162f9f22ad7d32a0b6e938a4803de2b7b1db124d730fcc929", 10, 0},
    {"camera.3", "739c21756c8f1002b37341a1c43383f795d5ae55df842140a11758d9f32f8a26", 8, 0},
    {"ctl.set", "9555d080d16d2575ff706111a50cc2876408b8ec4a6b3eef92b8b567e914eaa6", 13, 0},
    {"ctl.player.1", "27a99f01e114bf92a249aa3e2a3eb78731e143ca79f5f9ff38f1746f14f241f9", 8, 0},
    {"ctl.player.2", "4b67834a4c0fa5590d2e36e30f2e6904049935f543dbbfbaaa17ab7f47a4357d", 8, 0},
    {"ctl.row.0", "7699b56cc948a03f4fa01217314e0565f648af958cff1073008381e9c6fc3dc6", 14, 0},
    {"ctl.row.1", "f19b7f4e05f43178f2bd03224e06b4406fdf53b1f9207a88554f0cee9c37d440", 13, 0},
    {"ctl.row.2", "8b95ddcd7b3724467d4cbd44ad33c907ec8230cee10d35a2360e9b5bd3a93b93", 14, 0},
    {"ctl.row.3", "99e28e1f9ee82ace099b6e96ec08bf8cc280736a448aebc2925b99e55791e7f5", 15, 0},
    {"ctl.row.4", "2aca6670b3f0a5318c280ab4b2cea56c70d6cc244e05a14357c1e9c4ad436448", 8, 0},
    {"ctl.row.5", "de68fb991603c1420ec17060de521fc60ef1e974e1d95b484217e1c58b53da69", 11, 0},
    {"ctl.row.6", "9869fb3f1d0b18396199a15efe8947783059e947f901aa489168aa097ab99ffe", 12, 0},
    {"ctl.row.7", "b56cedbfb1e74dc7987cf5e47b2b1dc99d89eb50934a2f0a5e4c1fbf81f89a8b", 13, 0},
    {"ctl.row.8", "4f165b969549d5ec444ab2e482c5b9137327c0aa34b74ac2121635449acb9f24", 9, 0},
    {"ctl.row.9", "338f0879bcb2333ec5c3eed1e3b2b1640bf606b5b1f578a82c32e568769276d4", 10, 0},
    {"info.page", "a7c0b8328e8f9e71a6d164fb50b7915079030a4d0becd47fd1eeaafd00b65144", 5, 0},
    {"info.hint.prev", "99d9b5f5a15514edbf34a9b91332923a3d8ec955ced0de426e4f2269503ee5ea", 20, 0},
    {"info.hint.next", "e58a30fd4c2f132dc7cb00df42838d8d2a06a5eb8635eb61348f2a91b04635ba", 18, 0},
    {"info.pages.1", "272e97e77fda92758264ac36b1f2658da6c86335c1fce97b221ac6eba17257d0", 6, 0},
    {"info.pages.2", "adc67c657d449280360395d12f725030e59bab6e34966b6df46f7cb11d456fca", 6, 0},
    {"info.pages.3", "38b035cd8b8a241c759783e8c01bd5c1adeafcad6641af017966232384da8fac", 6, 0},
    {"info.pages.4", "b7780057879e63693ee650bd65bdb5bd0f44c18eaa0af75f97fd9b98396bd1cd", 6, 0},
    {"info.pages.5", "74509391f37b30c835570bc9fe7ca2db8656f9832651bfcca44a35dd2b02fb71", 6, 0},
    {"info.pages.6", "845e2303f4cfaf98d785b025d18d443b6f671f6bf44a343b29e4dd6dc9f77721", 6, 0},
    {"info.pages.7", "867818973f3abd9f61d004ffe78def8a3b2ccd494ec4ad20cd9803985fe29536", 6, 0},
    {"info.1.title", "709276c8392f940bfd855824583be68f48be48f22a303008784cc51aebc2c08f", 8, 0},
    {"info.1.0", "ea285ee753d3e832e7a6050c949811302b86f1e734160e8ce9a14ab4d967a0e6", 65, 0},
    {"info.1.1", "56ac180acdf5d42baa570cc1c40eff9458866b036e1b741920b4d6c10f2ecc4c", 63, 0},
    {"info.1.2", "3d9a8fbc568c449a1db4a87510e38962c002d5bd35b43ea6d8af162752ac85fe", 61, 0},
    {"info.1.3", "c8c73f3fb838babc995b4b9561e763402798b4a386cd8f2c41333a1d5c86eda1", 15, 0},
    {"info.1.5", "02b5dff0b6d92eca91ae710bbad0557940bac03343cd648a41f2388cef7c4c4a", 61, 0},
    {"info.1.6", "d48e98a15472f7c7ca9d40ebec072df71655a5e30121720994644fba8fbe27ef", 71, 0},
    {"info.1.8", "f0fe3fa39c4ab78da6b7994a19193c347c42ba4ed87691ab32bc544079ccc546", 66, 0},
    {"info.1.9", "41fe95f1f5d4d167439cf250d215e52da5f5f403f6d665dfa98ebb927375b832", 66, 0},
    {"info.1.10", "35133d61aabaa8605fd5e2ab7e9aeb1970bfc733c9c5e4ccf59de36a9993f78f", 66, 0},
    {"info.1.11", "221049039a1d8896f7a8d8fe4efe378efbb7fd4647722373293c3c490179c44b", 17, 0},
    {"info.2.title", "1fffada94d2c3299fc9ca34153b697bc55568b60ee44f195371f9ee2d3bab0cf", 29, 0},
    {"info.2.0", "e9f75b3efdb9aae85a6d7844d7a0b4246ad7694095078f285edacb64cbf7f93d", 13, 0},
    {"info.2.1", "28d6877a0e7498084b6cc00f1e065e4ec1b787cd8cf1a0b3f63c5e2bfeaca011", 57, 0},
    {"info.2.2", "485f1d8589320bbe15c60b4f0508ed781a3ad361591e0f66e08a3ba4d0ad061d", 58, 0},
    {"info.2.3", "108441af9b058980f53321dd045e9055f13015859fccc8d4c8f1361ba2fcf6c4", 26, 0},
    {"info.2.5", "940180ea9dae63794c24616acb7083afa6078e1ccf5b7515b9d27d69611a7fe9", 13, 0},
    {"info.2.6", "205c1f2be9f2e8c5360c18831dcbc4d3d3b6fea7791d1c652e77a3ebf7de61d7", 57, 0},
    {"info.2.7", "e32c48d3d2cde375fbd1b4b2715a64d3e2f760f05c97750aad6f16bf6d65802d", 17, 0},
    {"info.2.9", "66bc482924d34f836f8ba8cf99d3c15cd0a203a926d825a08ff3eb7d465d680a", 15, 0},
    {"info.2.10", "35a3638e63c520dc3c5e6021d3da53659e3d412f3faaacff1f21d81359d4a07f", 49, 0},
    {"info.2.11", "66a98da5e11153029bcbebd740f709cdce3b3b0e9436c36eb15b995bfad6bca8", 58, 0},
    {"info.2.13", "ce580082666ebb3feab080cc28835d1e89858ff5fa141875dee471a59c79da08", 13, 0},
    {"info.2.14", "3fc46d90bf66060bc474852720cd4015a90f9753abbecbdcf1eb88ac15233b72", 55, 0},
    {"info.2.15", "13a21272f47cebf758829c9341dc087cafe51e0620da2dd39072e0c94e668c0a", 23, 0},
    {"info.3.title", "914fd233d92f08a347aac72b684150a528b4df66138017c480bdbeee9d8af429", 29, 0},
    {"info.3.0", "02fc2f1cc8bdbf7f08a1973fa3de1439138204b3eff288e3410baa440624f69d", 11, 0},
    {"info.3.1", "8a22e5dffdff77beabdd3103e07eacb3301cbaa1b8e215bb714c65bdb7017327", 61, 0},
    {"info.3.2", "896b12ad6ecb9c4d06b5c022d9f0f199547999b42ad3f1f75badacf886a21b98", 56, 0},
    {"info.3.3", "78bfa514bb1d62acaeab49e37c7755c636eb3b661e6e92aa22e32524679c1671", 55, 0},
    {"info.3.4", "e721e452eff0808c6ac6f6d77ff21b8ab0f73136bbc37f4f3e1e510d31a69685", 28, 0},
    {"info.3.6", "1fa400010cce4b60561b362ce5f7f4d2fbbe73db2b04246a306f48b9b0d7b48d", 15, 0},
    {"info.3.7", "d726aaafa0253a24494344182d99fe95d42e5e6bb236f0b9daf864c1717d7a1f", 57, 0},
    {"info.3.8", "87cf81fdd2f56ff5abdf53814bae24ce3dc2bfe9b81d47d9356e9896bf40cf4e", 45, 0},
    {"info.3.10", "a67da37b00808c479ae85d40680b31aa3faa6cfb607ea14094d1f2dc1debbb85", 10, 0},
    {"info.3.11", "32de08f5670a18d703a3db7baa52bfd5416d5ff63ce413fc3ff0b6ecd768c2d8", 47, 0},
    {"info.3.12", "bc7b068e2463e4ce76b55d212239cd9313ee1551d71348432cd6b60a97499a35", 16, 0},
    {"info.5.title", "3bc9c521b514763db1982ee4b5aaa712cf8c77c94a85eb4a22cae9aefb2183cd", 22, 0},
    {"info.5.0", "bade0e396be81863cdd900fe9ec70e666ac2ea88f04027c06115601d95717c74", 16, 0},
    {"info.5.1", "5339212530f36a34bb534c5be9515b5889398393fe95da0b175a6288ff08be7e", 58, 0},
    {"info.5.2", "1a97be088ae53fd404e3f055a6654d415f180ad0e9841b81a8b56799bdd2f122", 60, 0},
    {"info.5.4", "a770689b6125453b05b771b1d6d5797070cfb904760b704b0b8e8022499d7afd", 14, 0},
    {"info.5.5", "e762b3679bab00281936c50fa310316356b87c9bcd0ed965b0467b6498724419", 53, 0},
    {"info.5.7", "88ee541402d0e65d8afc3f4ede2cb1e0d9dea94424fe99719e8f10ab29345c74", 29, 0},
    {"info.5.8", "c34225f08b1e102f222ca8883def1402b33b44cece899ffc4d548695c58bff1b", 60, 0},
    {"info.5.9", "cc38a7acb0709496c6799e1af626c44e6c7c63eddcb03f58b5134fcc3c02093b", 57, 0},
    {"info.5.10", "a85faa09e43d645115cbd4d2267d51d2b8954e117c7cf683d03726f1f3c75238", 7, 0},
    {"info.5.12", "d7fd51fae0c6a21f9d5e3f8d37f15058bb7d8fd0021dece553bc92ad9c037ff5", 27, 0},
    {"info.5.13", "3fd100e58fab04e0bc128408ef3f211bbef6cbc816807556efd50b95ae540ef1", 59, 0},
    {"info.5.14", "c88ee7f14db815d2ace7a570a48989715d4a0c921df527310b2f1991708a0368", 54, 0},
    {"info.5.15", "2e42fb8b3e0da71eea57dae17df4820ac6f83c536eab159207ec71df70b6fe12", 40, 0},
    {"info.6.title", "9e469dbdcbe64d0de8dd2d12317bc42c6750d15c1987cde5e3668502d3a0a522", 22, 0},
    {"info.6.0", "d299498dd3952632bdbf0e5c609afef0b32241ac48680c21cb036dc52a9e723e", 17, 0},
    {"info.6.1", "c5242450f016f4b72b146899baaea921c2dc37f84aad2bde7bb7f202b3ba9f32", 55, 0},
    {"info.6.2", "424e95bdd9b2b365664fc2d739d9dfeb0d44d38a5e89248a73df40185f5ca5b1", 51, 0},
    {"info.7.title", "c0b05ce018d274f937bcff98340681970e186193a82feb450eda6464e50f9192", 19, 0},
    {"info.7.0", "df541ddcf7e7753e5c65aefb3ce9d2262557c5a05d5d17e4d6197924a0f6a522", 13, 0},
    {"info.7.1", "93fb821cfd908df6610f2d0b3c6e7da4af739df6a4ff834308cf502415fedb58", 56, 0},
    {"info.7.2", "76dd4310fb2fbc3768c90a612bf08fe1e4710b4292b76749354c92f64ab0d564", 41, 0},
    {"info.7.4", "e0603a89ecb713fc40b4193412294aca22f3f3d8b8253cef345ca83a6c5a7dbc", 14, 0},
    {"info.7.5", "304f3075513d947d6b1aa2a94258e2c779e1639e9e62aedbdc80bde6a47ad4e9", 40, 0},
    {"info.7.7", "22c49d44a0299584a24a11c86a020aa0c43418f495e3cb56e6517caebd4e75d6", 15, 0},
    {"info.7.8", "b7446d24a504b94a62804b9f49fcedf0e565737c71f26c2567a0928ca3b63e8a", 56, 0},
    {"info.7.10", "1de03c7866277d05acff9f84af65d7ebb8899c4ed2a011f1e63df87296d24eb0", 16, 0},
    {"info.7.11", "38a119c87c51bec6985bcff7b2cd493c4514cc656b1ca0789f941d48ede15c06", 53, 0},
    {"info.7.12", "2d580054c9c057fa52e1f5f8b5f20cd0f99ff10e0878f69bbc1e265a0cef21df", 55, 0},
    {"info.7.13", "439e00410757f3c00dc7e2d05831ef022ca5d6d9bff8508eac59417889306ac9", 39, 0},
    {"info.8.title", "64b16c12e8201e80d820bed07fb74d5328f88fc3bbd2b6148903acda59f9357e", 19, 0},
    {"info.8.0", "f995032be78d5a7ce9ecfa36b2751dc14cecfe19953f421c30268359e173d53f", 18, 0},
    {"info.8.1", "c6af8e22eac512aab334c05efdf03f81063e6681dacbb2d751e577dbf4744c27", 48, 0},
    {"info.8.2", "13dc681617532fa137ff977d97d802ad0b77ef865dbe4601f9628bc450b96891", 46, 0},
    {"info.8.4", "c5180907847857e8314eddfe40206b3f47da013582c76cf591a6c26b7141f858", 13, 0},
    {"info.8.5", "efd2108422c54cc3a864c52a4e24c5fa40b7479d9d42e18680d50633b728b6bb", 43, 0},
    {"info.8.6", "ea421426bc2199d048842d9b4fb4766bd81af3082216647dc306fe9e87d310cc", 32, 0},
    {"credits.0", "f98f234f4d4856b25aca9c07491542c02d74483587017dff522eba9b0aceaa1d", 14, 0},
    {"credits.1", "287e8d1d2e705e047dbd8df6591b1923e2f0125b722b910f779f3a83801bab84", 17, 0},
    {"credits.3", "7813d8075fd6bab9c439bf9c92fcb1321870d4425515ce365ccc7237fd01cf1a", 11, 0},
    {"credits.4", "4ff8f00a35a38c3352764d4688cbb57d133908c4b5ddfc9a6e68ea49587ff109", 14, 0},
    {"credits.5", "aae4d3a92e8009064e16447486e70a15183712625ea5c1266be6fa42e9382112", 15, 0},
    {"credits.7", "6e6642d1311dde74c063e66cc03fb37a4acf9b1ea51940504bc1f4e53cb2b488", 26, 0},
    {"credits.8", "6aba6815d938beed82c2992a734beecb9a24b91d830c70cd05bd318cfbb0ad80", 15, 0},
    {"credits.9", "a2d6bb95a41879bec2db518d14762e8d5ba5b70e4ac9659c03c58c25afa96c37", 18, 0},
    {"credits.11", "9f7e01b9cd9753b86df08cfad8a29611321ad398d6c73310065ed4f1550adfee", 18, 0},
    {"credits.12", "21f5fe273aebb9c64e9b0a5c181e2689415dc9c2d022870228d3b030b1ed17da", 14, 0},
    {"credits.14", "dd25ca373419891465550e345700016ee9f13112e28407a0ed4c0d9c6c10e02a", 14, 0},
    {"credits.15", "87438dcb08ff27cb3cc1f0ccc7ff7d073beeee5445fb898423996326c2d5d35a", 16, 0},
    {"credits.16", "95c343c5b6847092da99ded565aee4d6a4c146158ec8440968dade9468c27e00", 16, 0},
    {"credits.18", "08d5f92ca9e9cecc2807e89018f28f8d705476ab927b7c4ca79bda0efbe4baab", 5, 0},
    {"credits.19", "2d037dfefc2c77d60c8277aebb9284c49cf122e68b9c58c0bcbcecdba2ed84eb", 24, 0},
    {"credits.21", "ac8fca60214a65f7c4894650b7663fa5a4d8b33d28763590a15c3d93fc2ebe2a", 13, 0},
    {"credits.22", "06033d4f502783cdece2e70e390b7dc603cdf222af19c8174abe0fec68c83f81", 20, 0},
    {"congrats.0", "6a9fac33b7c1e18c95bd33f3daeeb5dc4e9589257ad2ea8e1a6ee2bf2a9bb181", 16, 0},
    {"congrats.2", "7d7d0c6c827dbc59354bd99eb64ca72320b3d42da656b0cba05e0398e031c653", 57, 0},
    {"congrats.3", "9fe171f8440a27ad6bcb7ab93eeccb3b1dfca1ea4067a7aabddb92e8181f3620", 57, 0},
    {"congrats.4", "f5179b19c573b1a2f1957c1580b229edba9e53e8bbc78d1b670efb7076dabe68", 44, 0},
    {"congrats.6", "aded2152f22ec553f53d98518f9fc25904dec5139144404ad1cc3dd621760294", 64, 0},
    {"congrats.7", "f41ee8bae3205fd06366a78b69334cf6700f6359cc276c7e65ea2606face39af", 56, 0},
    {"congrats.8", "2d78ae29fe2124e8bfa81a2750f4b67cf25ed75d5607efa3ca335b68225fb6d6", 22, 0},
    {"congrats.10", "7011afa8d803afa67ccf212e18ca273d515c514d5d15b1cca4d48ae94757b5ec", 20, 0},
    {"loading.label", "dc380888c4e2c7762212480ff86eb39150ec70b45009c33bc6adcbd0041384b1", 7, 0},
    {"cheat.god_on", "3a77a0a8509a47fcdf52e88b4930d61d56df21fa23aa972de34a3d6909126d67", 17, 0},
    {"cheat.god_off", "8f0b1c75c77f8cd32a4a57d75692633ccf4f8a35ad6c1105ffda208d93e39227", 18, 0},
    {"cheat.lives", "3a035de717b5b7ae68f6eb2b39430a8d0cd08e7c559d47611f16c0613f46da29", 18, 0},
    {"cheat.weapons", "f1252ab4122cdd5678972d07fc4f4a2803de9ccb559bdf7d86862f0b2c983a75", 20, 0},
    {"cheat.missiles", "c9da398127e39125129538e8d63b65c700186df89c2f6da654652135d2b56f5f", 21, 0},
    {"cheat.powerups", "beaa0bbe1c9175c8873743ea78bd2bab45ad8fb0d6136dcd4bd467fdf2554943", 22, 0},
    {"dialog.10.start.0", "19bc975a25065c0d38e7dfec6a847cf84cfc460639ed383c8bdadaf237a2742a", 158, 0},
    {"dialog.10.start.0.speaker", "", 0, 1u},
    {"dialog.10.start.1", "66865415d21f7ad0b011e6b12d52b29066e1e0f641772fe66f167dfc0f8f2416", 154, 0},
    {"dialog.10.start.1.speaker", "", 0, 0u},
    {"dialog.10.start.2", "1b3944af8bb11e70a26d3b49838ae14b6553d3381c24f9e4579ce9a710498967", 131, 0},
    {"dialog.10.start.2.speaker", "", 0, 0u},
    {"dialog.10.end.0", "c602e85a1ceff628a4e4fd0f579c67bc86e21bdd5a2f1a30a7d58e2b86e3b2be", 103, 0},
    {"dialog.10.end.0.speaker", "", 0, 1u},
    {"dialog.10.end.1", "5be7b2ce823e2775750c06560c76c6f6083c2b847854173e1f63c544dee83020", 39, 0},
    {"dialog.10.end.1.speaker", "", 0, 0u},
    {"dialog.24.start.0", "06d12e33ef1dfab2763c28c38d1374af358bc7adfe6955a0f95c0aa00a83f821", 197, 0},
    {"dialog.24.start.0.speaker", "", 0, 0u},
    {"dialog.24.start.1", "4aead9510509f9dcbda4552fa5a5837933c8b479e9d13159403b60cc72f9c9c9", 116, 0},
    {"dialog.24.start.1.speaker", "", 0, 0u},
    // END gulf hash list
};

struct HashTable {
    GameId game;
    const HashedText* list;
    size_t count;
};
HashTable g_hashTables[kGameCount] = {
    {GameId::AirStrike3D, kAs3dHashes, sizeof(kAs3dHashes) / sizeof(kAs3dHashes[0])},
    {GameId::AirStrike2, kAs2Hashes, sizeof(kAs2Hashes) / sizeof(kAs2Hashes[0])},
    {GameId::GulfThunder, kGulfHashes, sizeof(kGulfHashes) / sizeof(kGulfHashes[0])},
};
const HashTable kBuiltinHashTables[kGameCount] = {
    {GameId::AirStrike3D, kAs3dHashes, sizeof(kAs3dHashes) / sizeof(kAs3dHashes[0])},
    {GameId::AirStrike2, kAs2Hashes, sizeof(kAs2Hashes) / sizeof(kAs2Hashes[0])},
    {GameId::GulfThunder, kGulfHashes, sizeof(kGulfHashes) / sizeof(kGulfHashes[0])},
};

constexpr size_t kHashMaxRun = 2000; // bytes of one NUL-terminated run looked at
constexpr double kHashMinShare = 0.6; // of a game's string keys: below it, no game executable

bool printableCp1251(std::uint8_t b) { return b == 0x09 || b == 0x0A || b == 0x0D || b >= 0x20; }

// AirStrike 3D v1.70 (frontend.md 3.9, 3.14, 5.11): Information pages as (page, first body
// string, title string); the body strings of a page lie from `body` up to its title.
struct V170Page {
    int page;
    std::uint32_t body, title;
};
const V170Page kV170Pages[10] = {
    {1, 0x449EA4, 0x44A1F8}, {2, 0x44A210, 0x44A430}, {3, 0x44A448, 0x44A688}, {4, 0x44A694, 0x44A850},
    {5, 0x44A870, 0x44AA94}, {6, 0x44AAB4, 0x44AB3C}, {7, 0x44AB5C, 0x44AD90}, {8, 0x44ADA8, 0x44AE28},
    {9, 0x44AE40, 0x44AFF0}, {10, 0x44AFF8, 0x44B07C}};
const std::pair<int, std::uint32_t> kV170Congrats[4] = {{0, 0x449D6C}, {2, 0x449D80}, {3, 0x449DBC}, {4, 0x449DF0}};
const std::uint32_t kV170RankTable = 0x45650C; // 7 pointers
const std::pair<const char*, std::uint32_t> kV170Hints[3] = {
    {"info.hint.prev", 0x44B084}, {"info.hint.next", 0x44B09C}, {"info.page", 0x44B0B0}};
const std::uint32_t kV170PageValues = 0x449E50; // "10 of 10" down to "1 of 10", 8-byte slots

struct ExeTable {
    const char* sha256;
    GameId game;
    const ListedText* listed; // nullptr: the v1.70 table above
    size_t listedCount;
};
const ExeTable kTables[] = {
    {"3b371bc2a72dcf18c17b5efa1b7e08b85fef73cfdd28dce00ec0aa2f2e93df1d", GameId::AirStrike3D, nullptr, 0},
    {"b24b62b2c5b61cfa1cf0aad781788aa777a2e4f4a385c73ba53014b039e46f5b", GameId::AirStrike2, kAs2List,
     sizeof(kAs2List) / sizeof(kAs2List[0])},
    {"86195a9653489064844c172ce43307c703a50e53be7e00d45fe346c45d5ae077", GameId::GulfThunder, kGulfList,
     sizeof(kGulfList) / sizeof(kGulfList[0])},
};

const ExeTable* findTable(const std::string& sha) {
    for (const ExeTable& t : kTables)
        if (sha == t.sha256) return &t;
    return nullptr;
}

// The PE image: virtual addresses to file offsets, strings and words at addresses. The first
// error is kept in `error` and every later read fails.
class Pe {
public:
    explicit Pe(const std::vector<std::uint8_t>& d) : data_(d) {
        if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') { setError("not an MZ executable"); return; }
        const std::uint32_t pe = rd32(0x3C);
        if (static_cast<std::uint64_t>(pe) + 24 > d.size() || std::memcmp(&d[pe], "PE\0\0", 4) != 0)
            { setError("no PE header"); return; }
        const unsigned nsec = rd16(pe + 6), opt = rd16(pe + 20);
        if (opt < 32 || nsec == 0 || nsec > 96) { setError("unexpected PE layout"); return; }
        base_ = rd32(pe + 24 + 28);
        size_t o = pe + 24 + opt;
        for (unsigned i = 0; i < nsec; ++i, o += 40) {
            if (o + 40 > d.size()) { setError("truncated section table"); return; }
            Section s;
            for (int k = 0; k < 8 && d[o + k]; ++k) s.name += static_cast<char>(d[o + k]);
            s.vsize = rd32(o + 8);
            s.va = rd32(o + 12);
            s.rsize = rd32(o + 16);
            s.roff = rd32(o + 20);
            sections_.push_back(s);
        }
    }
    bool ok() const { return error.empty(); }
    std::uint32_t base() const { return base_; }

    bool offset(std::uint32_t addr, size_t* out) {
        if (!ok()) return false;
        const std::int64_t rva = static_cast<std::int64_t>(addr) - base_;
        for (const Section& s : sections_) {
            const std::int64_t span = s.vsize < s.rsize ? s.vsize : s.rsize;
            if (s.va <= rva && rva < s.va + span) {
                const std::int64_t off = s.roff + rva - s.va;
                if (off < static_cast<std::int64_t>(data_.size())) {
                    *out = static_cast<size_t>(off);
                    return true;
                }
            }
        }
        setError("address 0x" + hex(addr) + " is not in the file");
        return false;
    }
    std::string cstr(std::uint32_t addr, size_t limit = 256, bool multiline = false) {
        size_t o = 0;
        if (!offset(addr, &o)) return std::string();
        size_t end = o;
        const size_t stop = o + limit + 1 < data_.size() ? o + limit + 1 : data_.size();
        while (end < stop && data_[end] != 0) ++end;
        if (end >= stop) {
            setError("no string terminator at 0x" + hex(addr));
            return std::string();
        }
        std::string s;
        for (size_t i = o; i < end; ++i) {
            const std::uint8_t b = data_[i];
            if ((b < 0x20 && !(multiline && b == 0x0A)) || b > 0x7E) {
                setError("non-text bytes at 0x" + hex(addr));
                return std::string();
            }
            s += static_cast<char>(b);
        }
        return s;
    }
    std::uint32_t u32(std::uint32_t addr) {
        size_t o = 0;
        if (!offset(addr, &o)) return 0;
        if (o + 4 > data_.size()) {
            setError("address 0x" + hex(addr) + " is not in the file");
            return 0;
        }
        return rd32(o);
    }
    std::uint8_t byteAt(std::uint32_t addr) {
        size_t o = 0;
        return offset(addr, &o) ? data_[o] : 0;
    }
    // The raw bytes of a section, clipped to the file.
    bool section(const char* name, size_t* begin, size_t* end) const {
        for (const Section& s : sections_)
            if (s.name == name) {
                *begin = s.roff < data_.size() ? s.roff : data_.size();
                const std::uint64_t e = static_cast<std::uint64_t>(s.roff) + s.rsize;
                *end = e < data_.size() ? static_cast<size_t>(e) : data_.size();
                return true;
            }
        return false;
    }
    const std::vector<std::uint8_t>& data() const { return data_; }
    void setError(const std::string& m) {
        if (error.empty()) error = m;
    }
    std::string error;

private:
    struct Section {
        std::string name;
        std::uint32_t va = 0, vsize = 0, roff = 0, rsize = 0;
    };
    static std::string hex(std::uint32_t v) {
        char b[16];
        std::snprintf(b, sizeof b, "%x", v);
        return b;
    }
    std::uint32_t rd16(size_t o) const { return data_[o] | (data_[o + 1] << 8); }
    std::uint32_t rd32(size_t o) const {
        return std::uint32_t(data_[o]) | (std::uint32_t(data_[o + 1]) << 8) | (std::uint32_t(data_[o + 2]) << 16) |
               (std::uint32_t(data_[o + 3]) << 24);
    }
    const std::vector<std::uint8_t>& data_;
    std::uint32_t base_ = 0;
    std::vector<Section> sections_;
};

using Entries = std::vector<std::pair<std::string, std::string>>;

// The line slot of a body string, from `mov dword [reg+disp8], imm32` storing its address in
// the page builder (extract_exe_texts.py line_slot); -1 if not found.
int lineSlot(const Pe& pe, size_t textBegin, size_t textEnd, std::uint32_t addr) {
    const std::vector<std::uint8_t>& d = pe.data();
    const std::uint8_t needle[4] = {static_cast<std::uint8_t>(addr), static_cast<std::uint8_t>(addr >> 8),
                                    static_cast<std::uint8_t>(addr >> 16), static_cast<std::uint8_t>(addr >> 24)};
    for (size_t p = textBegin; p + 4 <= textEnd; ++p) {
        if (std::memcmp(&d[p], needle, 4) != 0) continue;
        const size_t i = p - textBegin;
        int disp = -1;
        if (i >= 4 && d[p - 4] == 0xC7 && d[p - 3] == 0x44 && d[p - 2] == 0x24) disp = d[p - 1];
        else if (i >= 3 && d[p - 3] == 0xC7 && (d[p - 2] & 0xF8) == 0x40 && (d[p - 2] & 7) != 4) disp = d[p - 1];
        if (disp >= 0x14 && (disp - 0x14) % 4 == 0 && (disp - 0x14) / 4 < 32) return (disp - 0x14) / 4;
    }
    return -1;
}

bool extractV170(Pe& pe, Entries* entries) {
    if (pe.base() != 0x400000) {
        pe.setError("unexpected image base");
        return false;
    }
    size_t tb = 0, te = 0;
    if (!pe.section(".text", &tb, &te)) {
        pe.setError("no .text section");
        return false;
    }
    for (const V170Page& pg : kV170Pages) {
        entries->push_back({"info." + std::to_string(pg.page) + ".title", pe.cstr(pg.title)});
        std::vector<std::pair<std::uint32_t, std::string>> lines;
        std::uint32_t a = pg.body;
        while (a < pg.title && pe.ok()) {
            std::string s = pe.cstr(a);
            lines.push_back({a, s});
            a += static_cast<std::uint32_t>(s.size()) + 1;
            while (a < pg.title && pe.ok() && pe.byteAt(a) == 0) ++a;
        }
        if (!pe.ok()) return false;
        if (lines.empty()) {
            pe.setError("page " + std::to_string(pg.page) + " has no text");
            return false;
        }
        int next = 0;
        std::set<int> used;
        for (const auto& l : lines) {
            int slot = lineSlot(pe, tb, te, l.first);
            if (slot < 0 || used.count(slot)) slot = next;
            used.insert(slot);
            next = slot + 1;
            entries->push_back({"info." + std::to_string(pg.page) + "." + std::to_string(slot), l.second});
        }
    }
    for (const auto& c : kV170Congrats) entries->push_back({"congrats." + std::to_string(c.first), pe.cstr(c.second)});
    for (int i = 0; i < 7; ++i)
        entries->push_back({"rank." + std::to_string(i), pe.cstr(pe.u32(kV170RankTable + 4 * i), 32)});
    for (const auto& h : kV170Hints) entries->push_back({h.first, pe.cstr(h.second, 64)});
    for (int k = 0; k < 10; ++k)
        entries->push_back({"info.pages." + std::to_string(10 - k), pe.cstr(kV170PageValues + 8 * k + (k == 0 ? 0 : 4), 16)});
    return pe.ok();
}

bool extractListed(Pe& pe, const ExeTable& t, Entries* entries) {
    if (pe.base() != 0x400000) {
        pe.setError("unexpected image base");
        return false;
    }
    for (size_t i = 0; i < t.listedCount; ++i) {
        const ListedText& e = t.listed[i];
        std::string v;
        if (e.kind == 'u') v = std::to_string(pe.u32(e.address));
        else v = pe.cstr(e.address, 512, e.kind == 'm');
        entries->push_back({e.key, v});
    }
    if (t.game == GameId::AirStrike2) {
        for (auto& e : *entries)
            for (int r = 0; r < 10; ++r)
                if (e.first == "ctl.row." + std::to_string(r)) e.second = pe.cstr(kAs2CtlRows[r], 512);
    }
    return pe.ok();
}

// The texts of any build, by hash (issue 170 section 3; extract_exe_texts.py extract_by_hashes):
// every NUL-terminated run of printable cp1251 bytes whose length some dictionary has is hashed
// and looked up; the game with the best share of its string keys found wins, at kHashMinShare
// or more. `entries` gets the keys found in the dictionary's order (a u32 its value).
const HashTable* extractByHash(const std::vector<std::uint8_t>& exe, Entries* entries, double* share) {
    bool lengths[kHashMaxRun + 1] = {};
    std::unordered_map<std::string, std::vector<std::pair<int, size_t>>> wanted; // sha -> (table, entry)
    for (int t = 0; t < kGameCount; ++t)
        for (size_t i = 0; i < g_hashTables[t].count; ++i) {
            const HashedText& h = g_hashTables[t].list[i];
            if (!*h.sha256 || h.len == 0 || h.len > kHashMaxRun) continue;
            lengths[h.len] = true;
            wanted[h.sha256].push_back({t, i});
        }
    std::unordered_map<std::string, std::string> found; // sha -> the run
    size_t start = exe.size();
    for (size_t i = 0; i <= exe.size(); ++i) {
        const std::uint8_t b = i < exe.size() ? exe[i] : 0xFFu; // a last run without NUL is not terminated
        if (i < exe.size() && printableCp1251(b)) {
            if (start == exe.size()) start = i;
            continue;
        }
        if (i < exe.size() && b == 0 && start != exe.size()) {
            const size_t n = i - start;
            if (n >= 1 && n <= kHashMaxRun && lengths[n]) {
                const std::string h = sha256Hex(&exe[start], n);
                if (wanted.count(h) && !found.count(h)) found[h] = std::string(reinterpret_cast<const char*>(&exe[start]), n);
            }
        }
        start = exe.size();
    }
    const HashTable* best = nullptr;
    double bestShare = 0;
    for (int t = 0; t < kGameCount; ++t) {
        size_t strings = 0, hits = 0;
        for (size_t i = 0; i < g_hashTables[t].count; ++i) {
            const HashedText& h = g_hashTables[t].list[i];
            if (!*h.sha256) continue;
            ++strings;
            if (found.count(h.sha256)) ++hits;
        }
        const double s = strings ? static_cast<double>(hits) / static_cast<double>(strings) : 0.0;
        if (s > bestShare) {
            bestShare = s;
            best = &g_hashTables[t];
        }
    }
    if (share) *share = bestShare;
    if (!best || bestShare < kHashMinShare) return nullptr;
    for (size_t i = 0; i < best->count; ++i) {
        const HashedText& h = best->list[i];
        if (!*h.sha256) entries->push_back({h.key, std::to_string(h.value)});
        else if (found.count(h.sha256)) entries->push_back({h.key, found[h.sha256]});
    }
    return best;
}

std::string quote(const std::string& s) {
    std::string q = "\"";
    for (char c : s) {
        if (c == '\\') q += "\\\\";
        else if (c == '"') q += "\\\"";
        else if (c == '\n') q += "\\n";
        else q += c;
    }
    return q + "\"";
}

std::string textsFile(const GameProfile& g, const Entries& entries) {
    std::string out = std::string("# ") + g.title + " v" + g.version + " front-end texts, read from the user's executable by\n";
    out += "# tools/extract_exe_texts.py. Do not commit. Format: key = \"value\".\n";
    for (const auto& e : entries) out += e.first + " = " + quote(e.second) + "\n";
    return out;
}

} // namespace

const GameProfile* gameOfExecutable(const std::string& sha256) {
    const ExeTable* t = findTable(sha256);
    return t ? &gameProfile(t->game) : nullptr;
}

bool extractExeTextsByHash(const std::vector<std::uint8_t>& exe, const GameProfile** game, std::string* text,
                           std::string* error, double* share) {
    Entries entries;
    double s = 0;
    const HashTable* t = extractByHash(exe, &entries, &s);
    if (share) *share = s;
    if (!t) {
        if (error) {
            char pct[32];
            std::snprintf(pct, sizeof pct, "%.0f", s * 100.0);
            *error = std::string("not the executable of a known game (") + pct + " % of a game's texts in it at best, " +
                     std::to_string(static_cast<int>(kHashMinShare * 100.0)) + " % needed)";
        }
        return false;
    }
    const GameProfile& g = gameProfile(t->game);
    if (game) *game = &g;
    if (text) *text = textsFile(g, entries);
    return true;
}

bool extractExeTexts(const std::vector<std::uint8_t>& exe, const std::string& sha256, const GameProfile** game,
                     std::string* text, std::string* error) {
    const ExeTable* t = findTable(sha256);
    if (!t) return extractExeTextsByHash(exe, game, text, error, nullptr); // another build of a game, or no game
    const GameProfile& g = gameProfile(t->game);
    if (game) *game = &g;
    Pe pe(exe);
    Entries entries;
    const bool ok = pe.ok() && (t->listed ? extractListed(pe, *t, &entries) : extractV170(pe, &entries));
    if (!ok) {
        if (error) *error = pe.error;
        return false;
    }
    if (text) *text = textsFile(g, entries);
    return true;
}

void setExeTextHashesForTest(GameId game, const void* list, size_t count) {
    const int i = static_cast<int>(game);
    if (!list) {
        for (int k = 0; k < kGameCount; ++k) g_hashTables[k] = kBuiltinHashTables[k];
        return;
    }
    g_hashTables[i].list = static_cast<const HashedText*>(list);
    g_hashTables[i].count = count;
}

std::vector<std::string> exeTextHashListForTest(const char* key) {
    std::vector<std::string> out;
    for (int t = 0; t < kGameCount; ++t) {
        if (std::strcmp(gameProfile(g_hashTables[t].game).key, key) != 0) continue;
        for (size_t i = 0; i < g_hashTables[t].count; ++i) {
            const HashedText& h = g_hashTables[t].list[i];
            out.push_back(std::string(h.key) + "|" + h.sha256 + "|" + std::to_string(h.len) + "|" + std::to_string(h.value));
        }
    }
    return out;
}

std::vector<std::string> exeTextListForTest(const char* key) {
    std::vector<std::string> out;
    for (const ExeTable& t : kTables) {
        if (!t.listed || std::strcmp(gameProfile(t.game).key, key) != 0) continue;
        for (size_t i = 0; i < t.listedCount; ++i) {
            const ListedText& e = t.listed[i];
            const int kind = e.kind == 't' ? 0 : e.kind == 'm' ? 1 : 2;
            out.push_back(std::string(e.key) + "|" + std::to_string(e.address) + "|" + std::to_string(kind));
        }
    }
    return out;
}

} // namespace as3d
