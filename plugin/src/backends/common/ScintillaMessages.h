#pragma once

#include <windows.h>

// Keep the DLL independent of a particular Scintilla SDK checkout.  These are
// the small, documented part of the Scintilla message API used by VimEngine.
namespace Scintilla
{
constexpr UINT ADDTEXT            = 2001;
constexpr UINT INSERTTEXT         = 2003;
constexpr UINT DELETERANGE        = 2645;
constexpr UINT GETLENGTH          = 2006;
constexpr UINT GETCHARAT          = 2007;
constexpr UINT GETCURRENTPOS      = 2008;
constexpr UINT GETCOLUMN          = 2129;
constexpr UINT SETSEL             = 2160;
constexpr UINT FINDCOLUMN         = 2456;
constexpr UINT CLEARSELECTIONS    = 2571;
constexpr UINT SETSELECTION       = 2572;
constexpr UINT ADDSELECTION       = 2573;
constexpr UINT GOTOPOS            = 2025;
constexpr UINT POINTYFROMPOSITION = 2165;
constexpr UINT TEXTHEIGHT         = 2279;
constexpr UINT LINESCROLL         = 2168;
constexpr UINT ENSUREVISIBLE      = 2232;
constexpr UINT GETLINEVISIBLE     = 2228;
constexpr UINT SETZOOM            = 2373;
constexpr UINT ZOOMIN             = 2333;
constexpr UINT ZOOMOUT            = 2334;
constexpr UINT SETENDATLASTLINE   = 2277;
constexpr UINT GETENDATLASTLINE   = 2278;
constexpr UINT GETEOLMODE         = 2030;
constexpr UINT BEGINUNDOACTION    = 2078;
constexpr UINT ENDUNDOACTION      = 2079;
constexpr UINT GETLINEENDPOSITION = 2136;
constexpr UINT GETLINECOUNT       = 2154;
constexpr UINT LINEFROMPOSITION   = 2166;
constexpr UINT POSITIONFROMLINE   = 2167;
constexpr UINT REPLACESEL         = 2170;
constexpr UINT SETTEXT            = 2181;
constexpr UINT GETTEXT            = 2182;
constexpr UINT SETTARGETSTART     = 2190;
constexpr UINT SETTARGETEND       = 2192;
constexpr UINT REPLACETARGET      = 2194;
constexpr UINT DOCUMENTSTART      = 2316;
constexpr UINT DOCUMENTEND        = 2318;
constexpr UINT NEWLINE            = 2329;
constexpr UINT GETDOCPOINTER      = 2357;
constexpr UINT POSITIONBEFORE     = 2417;
constexpr UINT POSITIONAFTER      = 2418;
constexpr UINT GETCARETPERIOD     = 2075;
constexpr UINT SETCARETPERIOD     = 2076;
constexpr UINT SETCARETSTYLE      = 2512;
constexpr UINT GETCARETSTYLE      = 2513;
constexpr UINT EOL_CRLF           = 0;
constexpr UINT EOL_CR             = 1;
constexpr UINT EOL_LF             = 2;
constexpr UINT CARETSTYLE_LINE    = 1;
constexpr UINT CARETSTYLE_BLOCK   = 2;
} // namespace Scintilla
