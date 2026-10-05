// as3d::extractExeTexts (as3d/exe_texts.h): tools/extract_exe_texts.py in C++.
#include "as3d/exe_texts.h"

#include <cstdio>
#include <cstring>
#include <set>

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

} // namespace

const GameProfile* gameOfExecutable(const std::string& sha256) {
    const ExeTable* t = findTable(sha256);
    return t ? &gameProfile(t->game) : nullptr;
}

bool extractExeTexts(const std::vector<std::uint8_t>& exe, const std::string& sha256, const GameProfile** game,
                     std::string* text, std::string* error) {
    const ExeTable* t = findTable(sha256);
    if (!t) {
        if (error) *error = "not the executable of a known game";
        return false;
    }
    const GameProfile& g = gameProfile(t->game);
    if (game) *game = &g;
    Pe pe(exe);
    Entries entries;
    const bool ok = pe.ok() && (t->listed ? extractListed(pe, *t, &entries) : extractV170(pe, &entries));
    if (!ok) {
        if (error) *error = pe.error;
        return false;
    }
    std::string out = std::string("# ") + g.title + " v" + g.version + " front-end texts, read from the user's executable by\n";
    out += "# tools/extract_exe_texts.py. Do not commit. Format: key = \"value\".\n";
    for (const auto& e : entries) out += e.first + " = " + quote(e.second) + "\n";
    if (text) *text = out;
    return true;
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
