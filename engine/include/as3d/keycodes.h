// Key codes the engine uses without any SDL header: SDL2 scancode values (the engine's key
// numbering, see as3d/input.h) as plain ints. engine/src/input/input_mapper.cpp static_asserts
// that every value matches SDL's, so this header cannot drift from the SDL platform.
#pragma once

namespace as3d::keycodes {

constexpr int k0 = 39;
constexpr int k1 = 30;
constexpr int k2 = 31;
constexpr int k3 = 32;
constexpr int k4 = 33;
constexpr int k9 = 38;
constexpr int kA = 4;
constexpr int kAC_BACK = 270;
constexpr int kAPOSTROPHE = 52;
constexpr int kBACKSLASH = 49;
constexpr int kBACKSPACE = 42;
constexpr int kCAPSLOCK = 57;
constexpr int kCLEAR = 156;
constexpr int kCOMMA = 54;
constexpr int kDELETE = 76;
constexpr int kDOWN = 81;
constexpr int kEND = 77;
constexpr int kEQUALS = 46;
constexpr int kESCAPE = 41;
constexpr int kF1 = 58;
constexpr int kF12 = 69;
constexpr int kF13 = 104;
constexpr int kF24 = 115;
constexpr int kGRAVE = 53;
constexpr int kHOME = 74;
constexpr int kINSERT = 73;
constexpr int kKP_0 = 98;
constexpr int kKP_1 = 89;
constexpr int kKP_9 = 97;
constexpr int kKP_DIVIDE = 84;
constexpr int kKP_ENTER = 88;
constexpr int kKP_MINUS = 86;
constexpr int kKP_MULTIPLY = 85;
constexpr int kKP_PERIOD = 99;
constexpr int kKP_PLUS = 87;
constexpr int kLALT = 226;
constexpr int kLCTRL = 224;
constexpr int kLEFT = 80;
constexpr int kLEFTBRACKET = 47;
constexpr int kLSHIFT = 225;
constexpr int kMINUS = 45;
constexpr int kNUMLOCKCLEAR = 83;
constexpr int kP = 19;
constexpr int kPAGEDOWN = 78;
constexpr int kPAGEUP = 75;
constexpr int kPAUSE = 72;
constexpr int kPERIOD = 55;
constexpr int kRALT = 230;
constexpr int kRCTRL = 228;
constexpr int kRETURN = 40;
constexpr int kRIGHT = 79;
constexpr int kRIGHTBRACKET = 48;
constexpr int kRSHIFT = 229;
constexpr int kSCROLLLOCK = 71;
constexpr int kSEMICOLON = 51;
constexpr int kSLASH = 56;
constexpr int kSPACE = 44;
constexpr int kTAB = 43;
constexpr int kUP = 82;
constexpr int kZ = 29;

// SDL mouse button numbers (1 left, 2 middle, 3 right); a binding code is kMouseButtonCode (as3d/input.h) + one.
constexpr int kButtonLeft = 1, kButtonMiddle = 2, kButtonRight = 3;

} // namespace as3d::keycodes
