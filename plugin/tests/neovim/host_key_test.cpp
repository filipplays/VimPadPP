#include "host_key.h"
#include <iostream>

int main()
{
    bool bOk   = true;
    auto Check = [&](const char *pText, bool bExpected, UINT nKey = 0, bool bControl = false, bool bShift = false,
                     bool bAlt = false)
    {
        HostKey    sKey;
        const bool bParsed = HostKey::Parse(pText, sKey);
        if (bParsed != bExpected ||
            (bParsed && (sKey.nKey != nKey || sKey.bControl != bControl || sKey.bShift != bShift || sKey.bAlt != bAlt)))
        {
            std::cerr << "Unexpected parse result: " << pText << '\n';
            bOk = false;
        }
    };
    Check("<C-s>", true, 'S', true);
    Check("<C-Tab>", true, VK_TAB, true);
    Check("<C-S-Tab>", true, VK_TAB, true, true);
    Check("<s-c-tab>", true, VK_TAB, true, true);
    Check("<A-F4>", true, VK_F4, false, false, true);
    Check("<C-A-S-Delete>", true, VK_DELETE, true, true, true);
    Check("<F1>", true, VK_F1);
    Check("<F24>", true, VK_F24);
    Check("<C-0>", true, '0', true);
    Check("<Space>", true, VK_SPACE);
    Check("<CR>", true, VK_RETURN);
    Check("<Backspace>", true, VK_BACK);
    Check("<PageDown>", true, VK_NEXT);
    for (const char *pInvalid :
         {"", "Ctrl+S", "<>", "<C->", "<C-C-s>", "<M-s>", "<F0>", "<F01>", "<F25>", "<F100>", "<F1x>", "<C-NoSuchKey>",
          "<C-s>trailing", "<C-s><C-t>"})
        Check(pInvalid, false);

    BYTE aOriginal[256]{};
    GetKeyboardState(aOriginal);
    BYTE    aState[256]{};
    HostKey sSave;
    HostKey::Parse("<C-s>", sSave);
    aState[VK_CONTROL] = 0x80;
    SetKeyboardState(aState);
    bOk              = sSave.Matches('S') && !sSave.Matches('T') && bOk;
    aState[VK_SHIFT] = 0x80;
    SetKeyboardState(aState);
    bOk              = !sSave.Matches('S') && bOk;
    aState[VK_SHIFT] = 0;
    aState[VK_MENU]  = 0x80;
    SetKeyboardState(aState);
    bOk = !sSave.Matches('S') && bOk; // AltGr must not match Ctrl alone.
    SetKeyboardState(aOriginal);
    if (bOk)
        std::cout << "Host key checks passed\n";
    return bOk ? 0 : 1;
}
