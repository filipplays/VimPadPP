#pragma once

#include <string>
#include <windows.h>

// A physical shortcut, with exact modifier matching (independent of Vim mappings).
struct HostKey
{
    UINT nKey     = 0;
    bool bControl = false;
    bool bShift   = false;
    bool bAlt     = false;

    bool Matches(WPARAM nVirtualKey) const
    {
        return nVirtualKey == nKey && bControl == ((GetKeyState(VK_CONTROL) & 0x8000) != 0) &&
               bShift == ((GetKeyState(VK_SHIFT) & 0x8000) != 0) && bAlt == ((GetKeyState(VK_MENU) & 0x8000) != 0);
    }

    static bool Parse(std::string sKey, HostKey &sResult)
    {
        sResult = {};
        if (sKey.size() < 3 || sKey.front() != '<' || sKey.back() != '>')
            return false;
        sKey = sKey.substr(1, sKey.size() - 2);
        for (char &cKey : sKey)
            if (cKey >= 'a' && cKey <= 'z')
                cKey -= 'a' - 'A';
        while (sKey.size() > 2 && sKey[1] == '-')
        {
            bool *pModifier = sKey[0] == 'C'   ? &sResult.bControl
                              : sKey[0] == 'S' ? &sResult.bShift
                              : sKey[0] == 'A' ? &sResult.bAlt
                                               : nullptr;
            if (!pModifier || *pModifier)
                return false;
            *pModifier = true;
            sKey.erase(0, 2);
        }
        if (sKey.size() == 1 && ((sKey[0] >= 'A' && sKey[0] <= 'Z') || (sKey[0] >= '0' && sKey[0] <= '9')))
            sResult.nKey = static_cast<UINT>(sKey[0]);
        else if (sKey.size() >= 2 && sKey.size() <= 3 && sKey[0] == 'F' && sKey[1] >= '1' && sKey[1] <= '9')
        {
            UINT nFunction = sKey[1] - '0';
            if (sKey.size() == 3)
            {
                if (sKey[2] < '0' || sKey[2] > '9')
                    return false;
                nFunction = nFunction * 10 + sKey[2] - '0';
            }
            if (nFunction > 24)
                return false;
            sResult.nKey = VK_F1 + nFunction - 1;
        }
        else
        {
            const struct
            {
                const char *pName;
                UINT        nKey;
            } aNames[] = {{"TAB", VK_TAB},     {"SPACE", VK_SPACE},    {"CR", VK_RETURN},     {"ENTER", VK_RETURN},
                          {"BS", VK_BACK},     {"BACKSPACE", VK_BACK}, {"ESC", VK_ESCAPE},    {"LEFT", VK_LEFT},
                          {"RIGHT", VK_RIGHT}, {"UP", VK_UP},          {"DOWN", VK_DOWN},     {"HOME", VK_HOME},
                          {"END", VK_END},     {"PAGEUP", VK_PRIOR},   {"PAGEDOWN", VK_NEXT}, {"INSERT", VK_INSERT},
                          {"DEL", VK_DELETE},  {"DELETE", VK_DELETE}};
            for (const auto &sName : aNames)
                if (sKey == sName.pName)
                    sResult.nKey = sName.nKey;
        }
        return sResult.nKey != 0;
    }
};
