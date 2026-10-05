# notepad420

## 项目定位

Notepad4（zufuliu/notepad4）的个人 fork：Windows 10 LTSC 系统记事本替身。
核心差异：图片粘贴改造（P1–P4，见 CONTEXT.md）。不跟上游主线，只按需 cherry-pick。
领域术语见 CONTEXT.md。

## 常驻法则

- 项目可控的生成物只写入 `.target/`，不得向 `bin/`、`build/bin/` 等遗留根生成。
- 安装包写入 `.target/installer/`；便携 zip 写入 `.target/`，名字固定 `notepad420-setup-x64-<ver>.exe` / `notepad420_i18n_x64_v<ver>.zip`。发布后立刻刷新 `delivery.json` 指纹与清单——发布通道不得拎旧货（否则重花们目视不到差异）。
- 产物身份是 notepad420（exe/ini/标题/AppUserModelID）；代码内部标识符保持上游命名，不改（ADR 0003）。
- 粘贴缓存目录 `%TEMP%\notepad420-Paste\` 永不自动清理;触顶拒绝 + 状态栏提示,不弹模态框(ADR 0002)。
- 档位选定的粘贴由该档位独占:转换失败时丢弃剪贴板内容,不得回落为纯文本粘贴(ADR 0004)。
- Ctrl+Shift+V 属于反转粘贴；不得给 Visual Brace Matching 或其他功能重新绑定该键（ADR 0001）。
- 改动 `build/make_zip.bat` / `make_installer.iss` 后必须实际跑一次打包，并手动核对产物清单（zip 列表 / `7z l`）；仅看构建绿不验收打包内容（上一轮三个 zip bug 全是漏目检惹的祸）。
- 发布即二件：本地 `.target/` 交付与上游 GitHub Release 名目（`v<ver>` tag / 发布的那个提交）必须同一片源；不一致时停下来修版本链。
- 提交前等人工 review，不自行 commit。

## 按需读取索引

- 涉及领域术语 / 粘贴行为 / 命名边界时，读 `CONTEXT.md`。
- 构建 / 测试 / 打包 / 交付前，读 `.docs/spec/build-output.md`。

## 优先级

1. 用户当前明确指令。
2. 更近目录的 `AGENTS.md`。
3. 本文件。
4. 本文件路由到的 `.docs/*.md` 细则。