#pragma once
/**
 * ReplacerOps — replacer 的「替换 / 恢复 / 编辑器 / 关联」四个顶层动作的可复用实现。
 *
 * GUI(replacer.cpp) 与 CLI(wWinMain 的 --replace/--editor/--restore/--associate 分支)
 * 共用这几个函数，保证两条入口行为一致。
 *
 * 返回码约定（CLI 的进程退出码）:
 *   0 = 成功
 *   1 = 通用失败(细 error 里)
 *   3 = 未提权
 *   4 = 文件替换失败
 *   5 = 文件关联(UserChoice/ProgId/IFEO)失败
 *   6 = EDITOR/VISUAL 环境变量写入失败
 */

#include <string>

namespace ReplacerOps {

/// 是否以管理员提权运行。
bool IsElevatedSelf() noexcept;

/// 赋 privilege 集合（替换与恢复动作需要）。
bool EnableRequiredPrivileges() noexcept;

/// notepad420.exe 同目录（工具 exe 的目录）。
std::wstring SelfDir();
/// 同目录的 notepad420.exe 绝对路径。
std::wstring LocalNotepad420Path();

/// 替换全流程（文件 3 处 + locale + IFEO/UserChoice/ProgId）。
/// 不写 EDITOR/VISUAL；调用方需要时另调 RunEditorOperation。
/// 返回上述退出码，失败时 error 带人类可读描述。
int RunReplaceOperation(std::wstring &error);

/// 只写 EDITOR + VISUAL。
int RunEditorOperation(std::wstring &error);

/// 默认打开接管(--associate, ADR 0010):把 25 项清单的 UserChoice 重建为
/// Applications\notepad420.exe,与文件替换解耦。单项写失败即中止返回 5。
int RunAssociateOperation(std::wstring &error);

/// 恢复全流程（文件 3 处 + locale 回收 + UserChoice/ProgId 清理 + EDITOR/VISUAL 回滚）。
int RunRestoreOperation(std::wstring &error);

/// 测试接缝:重定向替换/恢复作用的文件目标集, nullptr 复位为系统三处。
void SetTargetsForTesting(const wchar_t *const *targets, size_t count);
/// 测试接缝:重定向 EDITOR/VISUAL 的备份注册表键, nullptr 复位。
void SetEditorBackupKeyForTesting(const wchar_t *key);

} // namespace ReplacerOps
