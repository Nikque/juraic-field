#pragma once
#include <cwchar>
#include <string>
namespace play::realgame {
// A user-selected window is retained until explicitly stopped. A remembered
// path was saved only after an earlier screen was identified.
// Neither selection authorizes any memory sample, pose, or input.
inline bool sameRememberedProgram14(const std::wstring& selected,const std::wstring& remembered){
    return !selected.empty() && !remembered.empty() && _wcsicmp(selected.c_str(),remembered.c_str())==0;
}
inline bool keepSelectedWindow14(bool userSelected,bool remembered){
    return userSelected || remembered;
}
}
