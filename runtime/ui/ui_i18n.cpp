#include "ui_i18n.h"

#include <string>

extern "C" {
#include "../settings.h"
}

namespace {

struct Entry {
    const char *zh_hans;
    const char *zh_hant;
    const char *en;
    const char *ja;
    const char *ko;
};

static const Entry k_table[STR_COUNT] = {
    // STR_APP_TITLE
    {"《十三机兵防卫圈》PC 重编译版", "《十三機兵防衛圈》PC 重編譯版", "13 Sentinels: Aegis Rim PC Recomp", "『十三機兵防衛圏』PC リコンパイル版", "『13기병방위권』 PC 리컴파일판"},
    // STR_APP_SUBTITLE
    {"合规自备资源启动前端", "合規自備資源啟動前端", "User-Supplied Assets Launcher", "ユーザー所有アセット用ランチャー", "사용자 보유 에셋 전용 런처"},
    // STR_NAV_HOME
    {"启动主页", "啟動主頁", "Home", "ホーム", "홈"},
    // STR_NAV_SETTINGS
    {"游戏设置", "遊戲設定", "Settings", "設定", "설정"},
    // STR_NAV_EXIT
    {"退出程序", "退出程式", "Exit", "終了", "종료"},
    // STR_BTN_CLOSE_MENU
    {"关闭菜单", "關閉選單", "Close Menu", "閉じる", "메뉴 닫기"},
    // STR_SYS_SETTINGS_TITLE
    {"系统设置", "系統設定", "Settings", "システム設定", "시스템 설정"},
    // STR_UI_LANG_LABEL
    {"界面语言", "介面語言", "UI Language", "UI言語", "UI 언어"},
    // STR_UI_LANG_DESC
    {"切换启动前端与系统设置界面的显示语言。修改后立即实时生效。", "切換啟動前端與系統設定介面的顯示語言。修改後立即即時生效。", "Switches the display language of the launcher and overlay menus immediately.", "ランチャーおよび設定メニューの表示言語を即座に切り替えます。", "런처 및 설정 메뉴의 표시 언어를 즉시 전환합니다."},
    // STR_GAME_LANG_LABEL
    {"游戏语言", "遊戲語言", "Game Language", "ゲーム言語", "게임 언어"},
    // STR_GAME_LANG_DESC_LAUNCHER
    {"选择游戏文本语言（Switch 系统语言服务）。亚版支持简体中文/繁體中文/韩文；欧美版支持 English/Français/Deutsch/Italiano/Español。设为“自动”时按版本自动适配。", "選擇遊戲文本語言（Switch 系統語言服務）。亞版支援簡體中文/繁體中文/韓文；歐美版支援 English/Français/Deutsch/Italiano/Español。設為「自動」時依版本自動適配。", "Selects the in-game text language (Asia: zh-Hans/zh-Hant/ko; US/EU: en-US/fr/de/it/es). Auto picks a matching default for the loaded version.", "ゲームへ通知する言語を設定します（アジア版: 簡体字/繁体字/韓国語、欧米版: EFIGS）。自動はバージョンに合わせて自動選択します。", "게임에 전달할 언어를 선택합니다(아시아판: 간체/번체/한국어, 북미/유럽판: EFIGS). 자동은 버전에 맞게 선택됩니다."},
    // STR_GAME_LANG_DESC_INGAME
    {"修改 Switch 系统语言设置并保存至 13sentinels.ini。\n\n提示：游戏语言将在重启游戏后生效。", "修改 Switch 系統語言設定並儲存至 13sentinels.ini。\n\n提示：遊戲語言將在重啟遊戲後生效。", "Saves the Switch system language to 13sentinels.ini.\n\nNote: Restart the game for language changes to take effect.", "Switch システム言語設定を 13sentinels.ini に保存します。\n\n※ ゲーム再起動後に反映されます。", "Switch 시스템 언어 설정을 13sentinels.ini에 저장합니다.\n\n※ 게임 재시작 후 적용됩니다."},
    // STR_GAME_LANG_RESTART_VAL
    {"重启后生效", "重啟後生效", "Restart req.", "要再起動", "재시작 필요"},
    // STR_GAME_LANG_SAVED_HINT
    {"游戏语言将在重启游戏后生效。", "遊戲語言將在重啟遊戲後生效。", "Game language will take effect after restarting the game.", "ゲーム言語の変更はゲーム再起動後に反映されます。", "게임 언어 변경 사항은 게임 재시작 후 적용됩니다."},

    // Launcher Home - Resource card
    {"游戏资源与校验状态", "遊戲資源與校驗狀態", "Game Files & Validation", "ゲームデータと検証ステータス", "게임 리소스 및 검증 상태"},
    {"当前数据工作区：", "當前資料工作區：", "Data Workspace: ", "データ作業ディレクトリ: ", "데이터 작업 경로: "},
    {"游戏资源目录：", "遊戲資源目錄：", "Game Directory: ", "ゲームフォルダ: ", "게임 폴더: "},
    {"（未设置）", "（未設定）", "(Not set)", "(未設定)", "(미설정)"},
    {"当前目录模式下已优先挂载本地便携目录：", "當前目錄模式下已優先掛載本地便攜目錄：", "Portable local extracted/ mounted: ", "ポータブル extracted/ を優先マウント中: ", "로컬 포터블 extracted/ 마운트됨: "},
    {"提示：当前目录中已存在 extracted/，回归与便携模式优先使用该目录。", "提示：當前目錄中已存在 extracted/，回歸與便攜模式優先使用該目錄。", "Note: extracted/ exists in the workspace and takes priority for portable/regression runs.", "注: extracted/ が存在するため、ポータブル/回帰モードで優先使用されます。", "참고: 작업 경로에 extracted/가 있어 포터블/회귀 모드로 우선 사용됩니다."},
    {"选择游戏目录...", "選擇遊戲目錄...", "Browse Folder...", "フォルダ選択...", "폴더 선택..."},
    {"重新校验", "重新校驗", "Re-validate", "再検証", "다시 검증"},
    {"打开游戏目录", "開啟遊戲目錄", "Open Folder", "フォルダを開く", "폴더 열기"},
    {"无法打开系统目录选择窗口，请检查桌面对话框服务或手动配置路径。", "無法開啟系統目錄選擇視窗，請檢查桌面對話框服務或手動配置路徑。", "Could not open folder picker dialog; please check desktop dialog services.", "フォルダ選択ダイアログを開けませんでした。", "폴더 선택 창을 열 수 없습니다."},

    // Validation messages
    {"尚未选择《十三机兵防卫圈》游戏资源目录", "尚未選擇《十三機兵防衛圈》遊戲資源目錄", "Game resource folder is not selected", "ゲームデータフォルダが未選択です", "게임 리소스 폴더가 선택되지 않았습니다"},
    {"请点击上方“选择游戏目录...”按钮，选择包含 exefs 与 romfs 文件夹的本地目录。", "請點擊上方「選擇遊戲目錄...」按鈕，選擇包含 exefs 與 romfs 資料夾的本地目錄。", "Click \"Browse Folder...\" and pick the extracted folder containing exefs/ and romfs/.", "「フォルダ選択...」から exefs と romfs を含むフォルダを選択してください。", "「폴더 선택...」을 눌러 exefs 및 romfs 폴더가 포함된 디렉터리를 선택하세요."},
    {"游戏目录不存在或无法访问", "遊戲目錄不存在或無法存取", "Game folder does not exist or is inaccessible", "ゲームフォルダが存在しないかアクセスできません", "게임 폴더가 없거나 접근할 수 없습니다"},
    {"配置路径：", "配置路徑：", "Configured path: ", "設定パス: ", "설정된 경로: "},
    {"检测到未解包镜像文件，请先解包为包含 exefs 与 romfs 的文件夹", "檢測到未解包鏡像檔案，請先解包為包含 exefs 與 romfs 的資料夾", "Packed image files detected; extract them to exefs/ and romfs/ folders first", "未展開のイメージファイルを検出しました。exefs / romfs へ展開してください", "압축/이미지 파일이 감지되었습니다. 먼저 exefs 및 romfs로 추출해 주세요"},
    {"支持的格式须已解包为 exefs/ 与 romfs/ 子目录，而非直接读取 .nsp / .xci / .nca / .nsz / .xcz 文件。", "支援的格式須已解包為 exefs/ 與 romfs/ 子目錄，而非直接讀取 .nsp / .xci / .nca / .nsz / .xcz 檔案。", "Use an extracted folder containing exefs/ and romfs/ instead of .nsp/.xci/.nca/.nsz/.xcz images.", ".nsp / .xci / .nca / .nsz / .xcz を直接指定することはできません。展開済みフォルダを指定してください。", ".nsp / .xci / .nca / .nsz / .xcz 대신 추출된 exefs/ 및 romfs/ 폴더를 지정하세요."},
    {"所选目录下未找到 exefs 与 romfs 文件夹", "所選目錄下未找到 exefs 與 romfs 資料夾", "Could not find exefs/ and romfs/ folders", "exefs と romfs フォルダが見つかりません", "exefs 및 romfs 폴더를 찾을 수 없습니다"},
    {"请确保所选目录（或其一级子目录）中同时包含 exefs/ 与 romfs/ 文件夹。", "請確保所選目錄（或其一級子目錄）中同時包含 exefs/ 與 romfs/ 資料夾。", "Make sure the selected folder (or an immediate subfolder) contains both exefs/ and romfs/.", "選択したフォルダ内に exefs と romfs の両方が含まれているか確認してください。", "선택한 폴더(또는 하위 1단계 폴더)에 exefs와 romfs가 모두 있는지 확인하세요."},
    {"exefs 关键文件缺失或损坏", "exefs 關鍵檔案缺失或損壞", "exefs files are missing or invalid", "exefs の必要ファイルが不足または破損しています", "exefs 필수 파일이 누락되었거나 손상되었습니다"},
    {"缺少文件：", "缺少檔案：", "Missing: ", "不足: ", "누락: "},
    {"文件头损坏或不可读：", "檔案頭損壞或不可讀：", "Invalid header: ", "不正/読込不可: ", "헤더 손상/읽기 불가: "},
    {"、", "、", ", ", "・", ", "},
    {"游戏版本不受支持（Build ID 不匹配）", "遊戲版本不受支援（Build ID 不匹配）", "Unsupported game version (Build ID mismatch)", "未対応バージョン（Build ID 不一致）", "지원되지 않는 버전(Build ID 불일치)"},
    {"检测到 exefs/main Build ID：%s\n当前支持：《十三机兵防卫圈》亚洲版 1.00 (31194702...)、欧美版 1.00 (57212E16...)、欧美版 1.10 (4B632573...)。", "檢測到 exefs/main Build ID：%s\n當前支援：《十三機兵防衛圈》亞洲版 1.00 (31194702...)、歐美版 1.00 (57212E16...)、歐美版 1.10 (4B632573...)。", "Found exefs/main Build ID: %s\nSupported: 13 Sentinels Asia 1.00 (31194702...), US/EU 1.00 (57212E16...), US/EU 1.10 (4B632573...).", "検出 Build ID: %s\n対応バージョン: アジア版 1.00 (31194702...)、欧米版 1.00 (57212E16...)、欧米版 1.10 (4B632573...)。", "감지된 Build ID: %s\n지원 버전: 아시아판 1.00 (31194702...), 북미/유럽판 1.00 (57212E16...), 북미/유럽판 1.10 (4B632573...)."},
    {"romfs 缺少核心资源包：romfs/ROBO_CN.CPK", "romfs 缺少核心資源包：romfs/ROBO_CN.CPK", "Missing core asset archive: romfs/ROBO_CN.CPK", "romfs/ROBO_CN.CPK が見つかりません", "핵심 리소스 파일 누락: romfs/ROBO_CN.CPK"},
    {"请确认已完整解包游戏 romfs 数据（未检测到 romfs/ROBO_CN.CPK）。", "請確認已完整解包遊戲 romfs 資料（未檢測到 romfs/ROBO_CN.CPK）。", "Ensure the full romfs contents are extracted (romfs/ROBO_CN.CPK was not found).", "romfs が完全に展開されているか確認してください（ROBO_CN.CPK 未検出）。", "romfs 데이터가 완전히 추출되었는지 확인하세요(ROBO_CN.CPK 미감지)."},
    {"romfs 缺少欧美版核心资源包：romfs/ROBO_US.CPK", "romfs 缺少歐美版核心資源包：romfs/ROBO_US.CPK", "Missing US/EU core archive: romfs/ROBO_US.CPK", "欧米版コアアーカイブ romfs/ROBO_US.CPK が見つかりません", "북미/유럽판 핵심 파일 누락: romfs/ROBO_US.CPK"},
    {"请确认已完整解包欧美版 romfs 数据（未检测到 romfs/ROBO_US.CPK）。", "請確認已完整解包歐美版 romfs 資料（未檢測到 romfs/ROBO_US.CPK）。", "Ensure US/EU romfs contents are extracted (romfs/ROBO_US.CPK was not found).", "欧米版 romfs が完全に展開されているか確認してください（ROBO_US.CPK 未検出）。", "북미/유럽판 romfs가 완전히 추출되었는지 확인하세요(ROBO_US.CPK 미감지)."},
    {"欧美版 1.10 更新包不完整：缺少 ROBO_PATCH_US.CPK", "歐美版 1.10 更新包不完整：缺少 ROBO_PATCH_US.CPK", "Incomplete US/EU 1.10 update: missing ROBO_PATCH_US.CPK", "欧米版 1.10 更新データ不完全: ROBO_PATCH_US.CPK 不足", "북미/유럽판 1.10 업데이트 불완전: ROBO_PATCH_US.CPK 누락"},
    {"检测到 exefs 为 1.10 更新版，但 romfs 中未找到 ROBO_PATCH_US.CPK。请使用基础本体 + 1.10 更新包重新合并解包。", "檢測到 exefs 為 1.10 更新版，但 romfs 中未找到 ROBO_PATCH_US.CPK。請使用基礎本體 + 1.10 更新包重新合併解包。", "exefs is v1.10, but romfs/ROBO_PATCH_US.CPK is missing. Re-extract Base + Update 1.10 together.", "exefs は 1.10 ですが romfs/ROBO_PATCH_US.CPK がありません。本体と 1.10 更新を同時に展開してください。", "exefs는 1.10이지만 romfs/ROBO_PATCH_US.CPK가 없습니다. 원본+업데이트를 함께 추출하세요."},
    {"游戏资源校验通过：亚洲版 1.00", "遊戲資源校驗通過：亞洲版 1.00", "Validation Passed: Asia 1.00", "検証完了: アジア版 1.00", "리소스 검증 완료: 아시아판 1.00"},
    {"游戏资源校验通过：欧美版 1.00（基础版）", "遊戲資源校驗通過：歐美版 1.00（基礎版）", "Validation Passed: US/EU 1.00 (Base)", "検証完了: 欧米版 1.00（ベース版）", "리소스 검증 완료: 북미/유럽판 1.00 (기본)"},
    {"游戏资源校验通过：欧美版 1.10（已合并更新）", "遊戲資源校驗通過：歐美版 1.10（已合併更新）", "Validation Passed: US/EU 1.10 (Updated)", "検証完了: 欧米版 1.10（アップデート適用済）", "리소스 검증 완료: 북미/유럽판 1.10 (업데이트됨)"},
    {"提示：当前为 1.00 基础版，可通过“导入 / 应用更新 (.nsp/.xci)...”合并 1.10 更新补丁。", "提示：當前為 1.00 基礎版，可透過「匯入 / 套用更新 (.nsp/.xci)...」合併 1.10 更新補丁。", "Tip: v1.00 Base detected. Use \"Import / Update (.nsp/.xci)...\" to merge the v1.10 update patch.", "ヒント: 1.00 ベース版です。「インポート / 更新 (.nsp/.xci)...」から 1.10 を適用できます。", "팁: 1.00 기본판입니다. \"가져오기 / 업데이트 (.nsp/.xci)...\"로 1.10 패치를 적용할 수 있습니다."},
    {"内置游戏语言：", "內建遊戲語言：", "Supported Languages: ", "対応言語: ", "지원 언어: "},
    {"提示：当前选择的“游戏语言”不受该版本支持，进入游戏后将由游戏回退到默认语言。", "提示：當前選擇的「遊戲語言」不受該版本支援，進入遊戲後將由遊戲回退到預設語言。", "Note: Selected Game Language is not in this version; the game will fall back to its region default.", "注意: 選択中のゲーム言語はこのバージョンに含まれないため、既定言語にフォールバックします。", "참고: 선택한 게임 언어는 현재 버전에서 지원되지 않아 기본 언어로 대체됩니다."},
    {"已检测到 ", "已檢測到 ", "Detected: ", "検出済み: ", "감지됨: "},
    {"✓ 游戏代码模块 (lasgame_*.dll)：已就绪，版本匹配", "✓ 遊戲程式碼模組 (lasgame_*.dll)：已就緒，版本匹配", "✓ Game module (lasgame_*.dll): Ready (version matched)", "✓ ゲームモジュール (lasgame_*.dll): 準備完了", "✓ 게임 코드 모듈 (lasgame_*.dll): 준비됨"},
    {"× 游戏代码模块：尚未在本地构建或版本不匹配", "× 遊戲程式碼模組：尚未在本地構建或版本不匹配", "× Game module: Not yet built (or version mismatch)", "× ゲームモジュール: 未構築または不一致", "× 게임 코드 모듈: 미빌드 또는 버전 불일치"},
    {"点击下方「本地构建游戏模块」或直接点击「开始游戏」，将使用自备资源在本机自动生成代码模块（完全合规，不联网下载版权资源）。", "點擊下方「本地構建遊戲模組」或直接點擊「開始遊戲」，將使用自備資源在本機自動生成程式碼模組。", "Click \"Build Game Module\" below or \"Start Game\" directly to compile locally from your own game files.", "「ゲームモジュールを構築」または「ゲーム開始」から、お持ちのデータを使ってローカルで自動構築します。", "아래 '게임 모듈 로컬 빌드' 또는 '게임 시작'을 누르면 보유한 파일로부터 로컬에서 자동 빌드됩니다."},
    {"本地构建游戏模块", "本地構建遊戲模組", "Build Game Module", "ゲームモジュールを構築", "게임 모듈 로컬 빌드"},

    // Launcher Home - NSP/XCI & Update Importer
    {"导入 / 应用更新 (.nsp/.xci)...", "匯入 / 套用更新 (.nsp/.xci)...", "Import / Update (.nsp/.xci)...", "インポート / 更新適用 (.nsp/.xci)...", "패키지 / 업데이트 가져오기 (.nsp/.xci)..."},
    {"导入游戏镜像与更新补丁 (NSP / XCI)", "匯入遊戲鏡像與更新補丁 (NSP / XCI)", "Import Game Image & Update Patch (NSP / XCI)", "ゲーム / アップデートパッチ適用 (NSP / XCI)", "게임 이미지 및 업데이트 적용 (NSP / XCI)"},
    {"合规声明：本程序不附带任何游戏资源、固件或解密密钥 (prod.keys)。请选择自备基础本体与可选更新补丁 (.nsp) 及 prod.keys，解包合并输出到目标文件夹（更新补丁为二进制差分格式，需搭配本体共同合并）。", "合規聲明：本程式不附帶任何遊戲資源、韌體或解密金鑰 (prod.keys)。請選擇自備基礎本體與可選更新補丁 (.nsp) 及 prod.keys，解包合併輸出到目標資料夾（更新補丁為二進位差分格式，需搭配本體共同合併）。", "Compliance Notice: No game assets, firmware, or decryption keys (prod.keys) are bundled. Supply your own legally owned Base (.nsp/.xci), optional Update (.nsp), and prod.keys to extract/merge into ExeFS & RomFS.", "コンプライアンス告知: ゲームデータや鍵 (prod.keys) は一切同梱されません。ご自身所有の本体 (.nsp/.xci)・更新 (.nsp)・prod.keys を指定して展開・統合してください。", "저작권 안내: 본 프로그램에는 게임 파일이나 복호화 키(prod.keys)가 포함되지 않습니다. 직접 보유한 원본(.nsp/.xci), 업데이트(.nsp), prod.keys를 선택해 병합 추출하세요."},
    {"基础本体 (.nsp/.xci)：", "基礎本體 (.nsp/.xci)：", "Base Image (.nsp/.xci): ", "ベース本体 (.nsp/.xci): ", "기본 이미지 (.nsp/.xci): "},
    {"更新补丁 (.nsp，可选)：", "更新補丁 (.nsp，可選)：", "Update Patch (.nsp, opt.): ", "更新パッチ (.nsp, 任意): ", "업데이트 (.nsp, 선택): "},
    {"密钥文件 (prod.keys)：", "金鑰檔案 (prod.keys)：", "Keys File (prod.keys): ", "キーファイル (prod.keys): ", "키 파일 (prod.keys): "},
    {"输出目录 (将生成 ExeFS/RomFS)：", "輸出目錄 (將生成 ExeFS/RomFS)：", "Output Folder (ExeFS/RomFS): ", "出力先フォルダ (ExeFS/RomFS): ", "출력 폴더 (ExeFS/RomFS): "},
    {"（未选择 — 仅提取基础本体）", "（未選擇 — 僅提取基礎本體）", "(None — extract base only)", "（未選択 — ベースのみ展開）", "(미선택 — 기본판만 추출)"},
    {"浏览...", "瀏覽...", "Browse...", "参照...", "찾아보기..."},
    {"清除", "清除", "Clear", "クリア", "지우기"},
    {"开始解包 / 合并更新", "開始解包 / 合併更新", "Extract / Apply Update", "展開・アップデート適用", "추출 / 업데이트 적용"},
    {"关闭", "關閉", "Close", "閉じる", "닫기"},
    {"取消任务", "取消任務", "Cancel", "キャンセル", "작업 취소"},
    {"✓ 解包工具就绪：", "✓ 解包工具就緒：", "✓ Extractor ready: ", "✓ 展開ツール準備完了: ", "✓ 추출 도구 준비됨: "},
    {"× 未检测到 nx_extract.exe，请直接选择已解包的 exefs/romfs 文件夹（或配置 nx_extract/ 目录 / LARECOMP_NX_EXTRACT）", "× 未檢測到 nx_extract.exe，請直接選擇已解包的 exefs/romfs 資料夾（或配置 nx_extract/ 目錄 / LARECOMP_NX_EXTRACT）", "× nx_extract.exe not found; please select an extracted folder containing exefs/romfs (or set LARECOMP_NX_EXTRACT).", "× nx_extract.exe が見つかりません。展開済みの exefs/romfs フォルダを直接選択してください。", "× nx_extract.exe를 찾을 수 없습니다. 추출된 exefs/romfs 폴더를 직접 선택해 주세요."},
    {"请先完整选择基础本体、prod.keys 与输出目录。", "請先完整選擇基礎本體、prod.keys 與輸出目錄。", "Please select Base image, prod.keys, and Output folder.", "ベース本体・prod.keys・出力フォルダをすべて指定してください。", "기본 이미지, prod.keys, 출력 폴더를 모두 지정해 주세요."},
    {"正在解包与校验合并数据（大型镜像可能需要 15~60 秒），请稍候...", "正在解包與校驗合併資料（大型鏡像可能需要 15~60 秒），請稍候...", "Extracting and verifying merged NCA sections (15-60s), please wait...", "展開および整合性検証中 (15~60秒)...", "추출 및 병합 검증 중 (15~60초)... 잠시 기다려 주세요."},
    {"✓ 解包并应用成功！已自动切换到输出目录并完成校验。", "✓ 解包並套用成功！已自動切換到輸出目錄並完成校驗。", "✓ Extraction & merge succeeded! Game directory updated.", "✓ 展開・適用に成功しました。ゲームフォルダを更新しました。", "✓ 추출 및 적용 완료! 게임 폴더가 자동으로 전환되었습니다."},
    {"× 解包失败（退出码 %d），请检查密钥与镜像文件：", "× 解包失敗（退出碼 %d），請檢查金鑰與鏡像檔案：", "× Extraction failed (exit %d); check keys and image files:", "× 展開失敗 (終了コード %d)。キーとイメージを確認してください:", "× 추출 실패 (종료 코드 %d). 키와 이미지 파일을 확인하세요:"},
    {"已取消解包任务。", "已取消解包任務。", "Extraction cancelled.", "展開をキャンセルしました。", "추출 작업이 취소되었습니다."},

    // Play card
    {"开始游戏", "開始遊戲", "Start Game", "ゲーム開始", "게임 시작"},
    {"请先确保上方游戏资源校验通过；若尚未生成代码模块，点击「开始游戏」将自动完成本地构建并启动。", "請先確保上方遊戲資源校驗通過；若尚未生成程式碼模組，點擊「開始遊戲」將自動完成本地構建並啟動。", "Ensure game resource validation passes; missing game modules will build locally and launch automatically.", "リソース検証が完了すると開始できます（未構築モジュールは自動ビルドされます）。", "리소스 검증이 완료되면 시작할 수 있습니다(미빌드 모듈은 자동 빌드 후 실행됩니다)."},
    {"下次启动跳过前端直接进入游戏", "下次啟動跳過前端直接進入遊戲", "Skip launcher on next startup", "次回起動時にランチャーをスキップ", "다음 실행 시 런처 건너뛰기"},
    {"（启动时按住 Shift 可强制重新打开本前端）", "（啟動時按住 Shift 可強制重新開啟本前端）", "(Hold Shift while launching to reopen launcher)", "(起動時に Shift キー長押しで再表示)", "(실행 시 Shift 키를 누르면 런처 다시 열기)"},

    // Save card
    {"存档快捷管理", "存檔快捷管理", "Save Management", "セーブデータ管理", "세이브 빠른 관리"},
    {"✓ 已检测到存档：save/save_0/ROBO13.DAT", "✓ 已檢測到存檔：save/save_0/ROBO13.DAT", "✓ Save found: save/save_0/ROBO13.DAT", "✓ セーブデータ検出: save/save_0/ROBO13.DAT", "✓ 세이브 감지됨: save/save_0/ROBO13.DAT"},
    {"文件大小：%.2f MB（%llu 字节）", "檔案大小：%.2f MB（%llu 位元組）", "Size: %.2f MB (%llu bytes)", "サイズ: %.2f MB (%llu バイト)", "파일 크기: %.2f MB (%llu 바이트)"},
    {"修改时间：%s", "修改時間：%s", "Modified: %s", "更新日時: %s", "수정 시간: %s"},
    {"未检测到存档（save/save_0/ROBO13.DAT）", "未檢測到存檔（save/save_0/ROBO13.DAT）", "No save found (save/save_0/ROBO13.DAT)", "セーブデータ未検出 (save/save_0/ROBO13.DAT)", "세이브 없음 (save/save_0/ROBO13.DAT)"},
    {"进入游戏并手动保存后，存档将保存在程序数据目录。", "進入遊戲並手動儲存後，存檔將儲存在程式資料目錄。", "After saving in-game, save files are stored inside the workspace save/ folder.", "ゲーム内でセーブすると作業ディレクトリ内に保存されます。", "게임 내에서 저장하면 데이터 작업 경로에 보관됩니다."},
    {"打开存档目录", "開啟存檔目錄", "Open Save Folder", "セーブフォルダを開く", "세이브 폴더 열기"},
    {"一键备份存档", "一鍵備份存檔", "Backup Save", "今すぐバックアップ", "원클릭 백업"},
    {"备份成功：", "備份成功：", "Backup saved: ", "バックアップ完了: ", "백업 성공: "},
    {"备份失败：", "備份失敗：", "Backup failed: ", "バックアップ失敗: ", "백업 실패: "},
    {"就绪", "就緒", "Ready", "準備完了", "준비됨"},
    {"无存档", "無存檔", "Empty", "なし", "없음"},

    // Quick settings card
    {"启动前配置", "啟動前配置", "Pre-Launch Setup", "起動前設定", "실행 전 설정"},
    {"进入游戏前即可配置显示、超采样、垂直同步、游戏语言、音频声道与完整手柄/键鼠映射，自动保存至 13sentinels.ini。", "進入遊戲前即可配置顯示、超採樣、垂直同步、遊戲語言、音訊聲道與完整手把/鍵鼠映射，自動儲存至 13sentinels.ini。", "Configure display, supersampling, VSync, game language, audio, and full controller/KB&M bindings before launch.", "表示モード、超解像、VSync、言語、音声、キーコンフィグを起動前に設定できます。", "실행 전 화면, 업스케일, 수직 동기화, 게임 언어, 오디오 및 키 매핑을 설정할 수 있습니다."},
    {"画面与显示设置", "畫面與顯示設定", "Display & Video", "画面・表示設定", "화면 및 디스플레이"},
    {"音频与声道设置", "音訊與聲道設定", "Audio & Output", "オーディオ設定", "오디오 및 채널"},
    {"控制与键鼠改键", "控制與鍵鼠改鍵", "Controls & Remap", "操作・キー設定", "조작 및 키 매핑"},
    {"高级与系统选项", "進階與系統選項", "Advanced & System", "詳細・システム", "고급 및 시스템"},

    // Loading animation
    {"正在启动《十三机兵防卫圈》", "正在啟動《十三機兵防衛圈》", "Launching 13 Sentinels: Aegis Rim", "『十三機兵防衛圏』を起動中", "『13기병방위권』 실행 중"},
    {"正在校验游戏资源与版本配置...", "正在校驗遊戲資源與版本配置...", "Validating game assets and build table...", "ゲームデータとバージョンを検証中...", "게임 리소스 및 버전 확인 중..."},
    {"正在加载静态重编译核心模块 (lasgame_*.dll)...", "正在載入靜態重編譯核心模組 (lasgame_*.dll)...", "Loading recompiled game module (lasgame_*.dll)...", "リコンパイルモジュールを読み込み中...", "정적 리컴파일 모듈 로딩 중 (lasgame_*.dll)..."},
    {"正在装载可执行镜像与数据段 (exefs)...", "正在裝載可執行鏡像與資料段 (exefs)...", "Loading executable images (exefs)...", "実行イメージを読み込み中 (exefs)...", "실행 이미지 로드 중 (exefs)..."},
    {"正在初始化主机内核服务与 Vulkan 渲染管线...", "正在初始化主機核心服務與 Vulkan 渲染管線...", "Initializing Switch services and Vulkan pipelines...", "システムサービスと描画パイプラインを初期化中...", "시스템 서비스 및 Vulkan 파이프라인 초기화 중..."},
    {"即将进入游戏画面，请稍候...", "即將進入遊戲畫面，請稍候...", "Preparing first frame, please wait...", "まもなくゲーム画面が表示されます...", "곧 게임 화면으로 진입합니다..."},
    {"系统装载中，请勿关闭窗口", "系統裝載中，請勿關閉視窗", "Booting runtime, please wait...", "起動処理中...", "로딩 중입니다. 잠시만 기다려 주세요"},

    // Tab names
    {"画面与显示", "畫面與顯示", "Display", "画面表示", "화면"},
    {"音频设置", "音訊設定", "Audio", "オーディオ", "오디오"},
    {"控制与键位映射", "控制與按鍵映射", "Controls", "コントローラー", "컨트롤"},
    {"高级与系统", "進階與系統", "System", "システム", "시스템"},

    // Display Tab
    {"显示与窗口", "顯示與視窗", "Display & Window", "表示とウィンドウ", "화면 및 창"},
    {"显示模式", "顯示模式", "Display Mode", "表示モード", "표시 모드"},
    {"窗口化", "視窗化", "Windowed", "ウィンドウ", "창 모드"},
    {"无边框全屏", "無邊框全螢幕", "Borderless", "ボーダーレス", "테두리 없는 전체"},
    {"独占全屏", "獨佔全螢幕", "Fullscreen", "フルスクリーン", "독점 전체 화면"},
    {"窗口化：按目标分辨率缩放；无边框全屏：铺满屏幕；独占全屏：切换独占显示模式。可用快捷键快速切换。", "視窗化：按目標解析度縮放；無邊框全螢幕：鋪滿螢幕；獨佔全螢幕：切換獨佔顯示模式。可用快捷鍵快速切換。", "Windowed, Borderless Fullscreen, or Exclusive Fullscreen display modes (also toggleable via hotkey).", "ウィンドウ / ボーダーレス / フルスクリーンを切り替えます。", "창 / 테두리 없는 전체 화면 / 독점 전체 화면을 선택합니다."},
    {"输出分辨率", "輸出解析度", "Output Resolution", "出力解像度", "출력 해상도"},
    {"自动", "自動", "Auto", "自動", "자동"},
    {"窗口化与独占全屏下优先使用的输出分辨率；窗口化超出桌面工作区时将自动限制大小。", "視窗化與獨佔全螢幕下優先使用的輸出解析度；視窗化超出桌面工作區時將自動限制大小。", "Preferred output resolution in windowed and fullscreen modes (clamped cleanly to desktop work area).", "ウィンドウ/フルスクリーン時の目標解像度を設定します。", "창 및 전체 화면의 목표 출력 해상도를 설정합니다."},
    {"画面比例", "畫面比例", "Aspect Ratio", "アスペクト比", "화면 비율"},
    {"锁定 16:9", "鎖定 16:9", "Locked 16:9", "16:9 固定", "16:9 고정"},
    {"铺满拉伸", "鋪滿拉伸", "Stretch", "画面に合わせる", "화면 채우기"},
    {"锁定 16:9：保持十三机兵原生 16:9 比例居中留黑边；铺满拉伸：铺满当前输出窗口。", "鎖定 16:9：保持十三機兵原生 16:9 比例置中留黑邊；鋪滿拉伸：鋪滿當前輸出視窗。", "Locked 16:9 preserves native aspect ratio with letterbox/pillarbox bars; Stretch fills the window.", "16:9 固定はアスペクト比を維持して黒帯を表示し、ストレッチはウィンドウ全体に表示します。", "16:9 고정은 원본 비율을 유지하며, 화면 채우기는 창 전체에 맞춥니다."},
    {"垂直同步", "垂直同步", "VSync Mode", "垂直同期 (VSync)", "수직 동기화"},
    {"Mailbox 低延迟", "Mailbox 低延遲", "Mailbox (Low Latency)", "Mailbox (低遅延)", "Mailbox (저지연)"},
    {"FIFO 严格同步", "FIFO 嚴格同步", "FIFO (Strict VSync)", "FIFO (厳格同期)", "FIFO (엄격 동기)"},
    {"Immediate 关闭", "Immediate 關閉", "Immediate (Off)", "Immediate (オフ)", "Immediate (끄기)"},
    {"修改后立即重建 Vulkan 交换链：Mailbox 兼顾低延迟与无撕裂；FIFO 严格锁定刷新率；Immediate 关闭等待。", "修改後立即重建 Vulkan 交換鏈：Mailbox 兼顧低延遲與無撕裂；FIFO 嚴格鎖定更新率；Immediate 關閉等待。", "Applies immediately: Mailbox gives tear-free low latency; FIFO locks refresh rate; Immediate disables VSync.", "即時反映されます：Mailbox は遅延とティアリングを抑え、FIFO はリフレッシュレートに同期します。", "즉시 적용됩니다: Mailbox는 저지연 무티어링, FIFO는 주사율 동기, Immediate는 해제입니다."},

    {"内部渲染与超采样", "內部渲染與超採樣", "Internal Render Scale", "内部レンダリング", "내부 렌더링 배율"},
    {"内部渲染倍率", "內部渲染倍率", "Render Scale", "レンダリング倍率", "렌더링 배율"},
    {"1.0x 原生 (1080p)", "1.0x 原生 (1080p)", "1.0x Native (1080p)", "1.0x 等倍 (1080p)", "1.0x 원본 (1080p)"},
    {"1.5x (1440p) [实验性]", "1.5x (1440p) [實驗性]", "1.5x (1440p) [Exp]", "1.5x (1440p) [実験的]", "1.5x (1440p) [실험적]"},
    {"2.0x (4K) [实验性]", "2.0x (4K) [實驗性]", "2.0x (4K) [Exp]", "2.0x (4K) [実験的]", "2.0x (4K) [실험적]"},
    {"内部渲染倍率。\n\n1.0x 原生最稳定推荐；1.5x 与 2.0x 为实验性超采样，将大幅增加 GPU 负载。\n\n当前内部分辨率：%.0f × %.0f", "內部渲染倍率。\n\n1.0x 原生最穩定推薦；1.5x 與 2.0x 為實驗性超採樣，將大幅增加 GPU 負載。\n\n當前內部解析度：%.0f × %.0f", "Internal render multiplier.\n\n1.0x is recommended; 1.5x/2.0x increase GPU load significantly.\n\nCurrent internal resolution: %.0f x %.0f", "内部解像度倍率。\n1.0x が安定推奨。1.5x / 2.0x は高負荷です。\n現在: %.0f x %.0f", "내부 렌더링 배율입니다.\n1.0x 권장, 1.5x/2.0x는 GPU 부하가 큽니다.\n현재 내부 해상도: %.0f x %.0f"},
    {"图像缩放滤镜", "圖像縮放濾鏡", "Upscale Filter", "アップスケーラー", "업스케일 필터"},
    {"双线性 (Bilinear)", "雙線性 (Bilinear)", "Bilinear", "バイリニア", "바이리니어"},
    {"输出分辨率与内部分辨率不一致时使用的放大锐化滤镜。", "輸出解析度與內部解析度不一致時使用的放大銳化濾鏡。", "Scaling filter used when window resolution differs from internal render resolution.", "ウィンドウ解像度と内部解像度が異なる場合のスケーリングフィルタ。", "출력 해상도와 내부 해상도가 다를 때 사용하는 보간/샤프닝 필터입니다."},

    {"监控与运行节奏", "監控與運行節奏", "HUD & Speed", "パフォーマンスと速度", "성능 및 배속"},
    {"性能监控 HUD", "效能監控 HUD", "Performance HUD", "パフォーマンス HUD", "성능 모니터 HUD"},
    {"关闭", "關閉", "Off", "オフ", "끄기"},
    {"仅 FPS", "僅 FPS", "FPS Only", "FPS のみ", "FPS만 표시"},
    {"详细监控", "詳細監控", "Detailed", "詳細表示", "상세 모니터"},
    {"在画面右上角显示实时 FPS、帧耗时、绘制次数及 GPU 等待时长。", "在畫面右上角顯示即時 FPS、幀耗時、繪製次數及 GPU 等待時長。", "Shows real-time FPS, frame time, draw calls, and GPU sync wait in the top-right corner.", "右上に FPS・フレーム時間・描画数・GPU待機時間を表示します。", "화면 우상단에 FPS, 프레임 시간, 드로우 수, GPU 대기 시간을 표시합니다."},
    {"快进倍速控制", "快進倍速控制", "Speed Multiplier", "早送り倍率", "빠른 배속"},
    {"1x 正常", "1x 正常", "1x Normal", "1x 通常", "1x 보통"},
    {"2x 快进", "2x 快進", "2x Fast", "2x 早送り", "2x 배속"},
    {"3x 快进", "3x 快進", "3x Fast", "3x 早送り", "3x 배속"},
    {"动态调整运行倍速（默认 F3 切换）。开启快进时将自动静音，仅加速运行节奏。", "動態調整運行倍速（預設 F3 切換）。開啟快進時將自動靜音，僅加速運行節奏。", "Adjusts runtime speed dynamically (default hotkey F3). Audio automatically mutes while fast-forwarding.", "実行速度を変更します（F3で切替）。早送り中は自動的にミュートになります。", "실행 배속을 전환합니다(기본 단축키 F3). 배속 중에는 자동으로 음소거됩니다."},
    {"当前已通过环境变量 LARECOMP_SPEED_MULT 锁定倍速。快进期间将自动静音。", "當前已透過環境變數 LARECOMP_SPEED_MULT 鎖定倍速。快進期間將自動靜音。", "Speed is currently locked via LARECOMP_SPEED_MULT environment variable.", "環境変数 LARECOMP_SPEED_MULT により倍率が固定されています。", "환경 변수 LARECOMP_SPEED_MULT에 의해 배속이 고정되어 있습니다."},

    // Audio Tab
    {"声音与输出", "聲音與輸出", "Sound & Output", "サウンド設定", "사운드 및 출력"},
    {"主音量", "主音量", "Master Volume", "マスター音量", "마스터 볼륨"},
    {"调节游戏整体输出音量（0% ~ 100%）。快进倍速期间将自动临时静音。", "調節遊戲整體輸出音量（0% ~ 100%）。快進倍速期間將自動臨時靜音。", "Adjusts master game volume (0% to 100%). Temporarily muted while fast-forwarding.", "ゲーム全体の音量を調整します（早送り時は自動ミュート）。", "게임 전체 음량을 조절합니다(배속 중에는 임시 음소거)."},
    {"窗口失焦自动静音", "視窗失焦自動靜音", "Mute When Unfocused", "非アクティブ時ミュート", "창 비활성 시 음소거"},
    {"切换到其他软件窗口（失去焦点）时自动静音，切回游戏后立即恢复。", "切換到其他軟體視窗（失去焦點）時自動靜音，切回遊戲後立即恢復。", "Automatically mutes game audio when the window loses focus.", "ウィンドウが非アクティブになると自動的にミュートにします。", "창 포커스를 잃으면 자동으로 소리를 끕니다."},
    {"输出声道模式", "輸出聲道模式", "Audio Channels", "出力チャンネル", "오디오 출력 채널"},
    {"自动", "自動", "Auto", "自動", "자동"},
    {"立体声 (2.0)", "立體聲 (2.0)", "Stereo (2.0)", "ステレオ (2.0)", "스테레오 (2.0)"},
    {"5.1 环绕", "5.1 環繞", "5.1 Surround", "5.1 サラウンド", "5.1 서라운드"},
    {"自动：按音频设备选择；立体声：强制混音为双声道；5.1 环绕：直通 6 声道输出。", "自動：按音訊裝置選擇；立體聲：強制混音為雙聲道；5.1 環繞：直通 6 聲道輸出。", "Auto selects based on audio device; Stereo forces 2.0 downmix; 5.1 Surround outputs 6 channels.", "自動はデバイスに合わせ、ステレオは2ch、5.1は6ch出力を行います。", "자동은 오디오 장치에 따르며, 스테레오(2.0) 및 5.1 서라운드를 선택할 수 있습니다."},
    {"开启", "開啟", "On", "オン", "켜짐"},
    {"关闭", "關閉", "Off", "オフ", "꺼짐"},

    // System & Advanced Tab
    {"语言与界面", "語言與介面", "Language & UI", "言語とインターフェース", "언어 및 인터페이스"},
    {"底层渲染同步 [均推荐保持开启]", "底層渲染同步 [均推薦保持開啟]", "Vulkan Sync Options [Keep On]", "Vulkan 同期設定 [推奨: オン]", "Vulkan 동기화 옵션 [유지 권장]"},
    {"展开底层 Vulkan 同步开关", "展開底層 Vulkan 同步開關", "Show Advanced Vulkan Toggles", "詳細 Vulkan スイッチを表示", "고급 Vulkan 동기화 옵션 표시"},
    {"警告：下方三项用于保证十三机兵零闪烁与零画面错乱，除非研发排查否则建议全部保持开启。", "警告：下方三項用於保證十三機兵零閃爍與零畫面錯亂，除非研發排查否則建議全部保持開啟。", "Warning: these low-level sync guards prevent flicker and corruption in 13 Sentinels; keep all enabled.", "警告：ちらつきや描画崩れを防ぐ同期機能です。検証時以外はすべてオンを推奨します。", "경고: 깜빡임과 화면 깨짐을 방지하는 동기화 옵션입니다. 모두 켜둠을 권장합니다."},
    {"管线编译等待 [推荐保持开启]", "管線編譯等待 [推薦保持開啟]", "Wait Compiling Pipelines [Rec]", "パイプライン待機 [推奨]", "파이프라인 컴파일 대기 [권장]"},
    {"录制队列等待管线编译完成后再提交绘制，关闭可能导致闪烁。", "錄製佇列等待管線編譯完成後再提交繪製，關閉可能導致閃爍。", "Recorder waits for shader pipelines to finish compiling before recording draws.", "シェーダーパイプラインのコンパイル完了を待機してから記録します。", "녹화 스레드가 셰이더 파이프라인 컴파일 완료를 기다립니다."},
    {"顶点隔离拷贝 [推荐保持开启]", "頂點隔離拷貝 [推薦保持開啟]", "Copy Dynamic Vertices [Rec]", "頂点バッファ分離コピー [推奨]", "정점/인덱스 분리 복사 [권장]"},
    {"为每帧顶点与索引缓冲创建瞬态快照副本，关闭可能导致快进错乱。", "為每幀頂點與索引緩衝建立瞬態快照副本，關閉可能導致快進錯亂。", "Copies vertex/index buffers safely into transient GPU memory to prevent fast-forward corruption.", "頂点・インデックスデータをフレーム毎に安全にコピーします。", "정점 및 인덱스 데이터를 프레임별로 복사하여 경합을 방지합니다."},
    {"CPU-GPU 帧重叠 [推荐保持开启]", "CPU-GPU 幀重疊 [推薦保持開啟]", "CPU-GPU Frame Overlap [Rec]", "CPU-GPU オーバーラップ [推奨]", "CPU-GPU 프레임 중첩 [권장]"},
    {"允许 GPU 重叠处理帧命令以保持 60 FPS；关闭将显著掉帧。", "允許 GPU 重疊處理幀命令以保持 60 FPS；關閉將顯著掉幀。", "Allows GPU overlap for full 60 FPS throughput; disabling forces strict frame stalling.", "GPU 処理をオーバーラップして 60 FPS を維持します。", "GPU 처리를 중첩하여 60 FPS를 유지합니다."},
    {"着色器缓存管理", "著色器快取管理", "Shader Cache", "シェーダーキャッシュ", "셰이더 캐시 관리"},
    {"一键清理着色器缓存", "一鍵清理著色器快取", "Clear Shader Cache", "シェーダーキャッシュ削除", "셰이더 캐시 삭제"},
    {"清理 cache/spirv/ 与磁盘管线缓存。\n\n清理后游戏重新加载场景时会即时重新编译。", "清理 cache/spirv/ 與磁碟管線快取。\n\n清理後遊戲重新載入場景時會即時重新編譯。", "Clears cache/spirv/ and disk Vulkan pipeline caches; shaders will recompile automatically as needed.", "cache/spirv/ とパイプラインキャッシュを削除します。", "cache/spirv/ 및 Vulkan 파이프라인 캐시를 정리합니다."},
    {"已清理着色器缓存（移除 %d 个缓存文件）。", "已清理著色器快取（移除 %d 個快取檔案）。", "Cleared shader cache (%d files removed).", "キャッシュを削除しました（%d ファイル）。", "셰이더 캐시가 정리되었습니다(%d개 파일 삭제)."},
    {"系统与退出", "系統與退出", "System & Exit", "システムと終了", "시스템 및 종료"},
    {"返回启动前端", "返回啟動前端", "Return to Launcher", "ランチャーへ戻る", "런처로 돌아가기"},
    {"结束当前运行并返回启动前端界面（未保存的进度将丢失）。", "結束當前運行並返回啟動前端介面（未儲存的進度將遺失）。", "Stops the game and returns to the launcher (unsaved progress will be lost).", "ゲームを終了してランチャーへ戻ります（未保存データは失われます）。", "게임을 종료하고 런처로 돌아갑니다(저장하지 않은 진행은 손실됩니다)."},
    {"当前已在启动前端中。", "當前已在啟動前端中。", "Currently in the launcher.", "現在はランチャー画面です。", "현재 런처 화면입니다."},
    {"退出游戏", "退出遊戲", "Exit Application", "アプリを終了", "프로그램 종료"},
    {"保存当前配置并关闭程序（未保存的进度将丢失）。", "儲存當前配置並關閉程式（未儲存的進度將遺失）。", "Saves settings and closes 13SentinelsRecomp (unsaved progress will be lost).", "設定を保存してアプリを終了します（未保存データは失われます）。", "설정을 저장하고 프로그램을 종료합니다(미저장 데이터 손실)."},
    {"保存当前配置并关闭程序。", "儲存當前配置並關閉程式。", "Saves settings and exits 13SentinelsRecomp.", "設定を保存して終了します。", "설정을 저장하고 종료합니다."},
    {"确认返回启动前端", "確認返回啟動前端", "Confirm Return to Launcher", "ランチャーへ戻りますか？", "런처로 돌아가기 확인"},
    {"确认退出游戏", "確認退出遊戲", "Confirm Exit", "終了しますか？", "종료 확인"},
    {"未在游戏内主动保存的进度将会丢失，是否继续？", "未在遊戲內主動儲存的進度將會遺失，是否繼續？", "Progress since your last in-game save will be lost. Continue?", "未保存の進行状況は失われます。続行しますか？", "마지막 저장 이후의 진행 상황은 손실됩니다. 계속하시겠습니까?"},
    {"取消", "取消", "Cancel", "キャンセル", "취소"},
    {"留在当前界面。", "留在當前介面。", "Stay on current screen.", "現在の画面に留まります。", "현재 화면에 머뭅니다."},
    {"当前存档：save/save_0/ROBO13.DAT\n大小：%.2f MB（%llu 字节）\n最后修改：%s", "當前存檔：save/save_0/ROBO13.DAT\n大小：%.2f MB（%llu 位元組）\n最後修改：%s", "Save file: save/save_0/ROBO13.DAT\nSize: %.2f MB (%llu bytes)\nModified: %s", "セーブ: save/save_0/ROBO13.DAT\nサイズ: %.2f MB (%llu バイト)\n更新: %s", "세이브: save/save_0/ROBO13.DAT\n크기: %.2f MB (%llu 바이트)\n수정: %s"},
    {"当前未检测到存档（save/save_0/ROBO13.DAT 尚未生成）。", "當前未檢測到存檔（save/save_0/ROBO13.DAT 尚未生成）。", "No save file detected yet (save/save_0/ROBO13.DAT).", "セーブデータはまだありません (save/save_0/ROBO13.DAT)。", "세이브 파일이 아직 없습니다 (save/save_0/ROBO13.DAT)."},
    {"将当前存档复制备份到 save_backups/ 目录。", "將當前存檔複製備份到 save_backups/ 目錄。", "Copies the active save file into save_backups/.", "現在のセーブデータを save_backups/ へコピーします。", "현재 세이브 파일을 save_backups/ 폴더에 백업합니다."},

    // Controls UI
    {"已连接手柄：%s（共 1 个）", "已連接手把：%s（共 1 個）", "Controller Connected: %s (1 device)", "コントローラー接続済み: %s (1台)", "연결된 컨트롤러: %s (1대)"},
    {"已连接手柄：%s（共 %d 个设备）", "已連接手把：%s（共 %d 個裝置）", "Controller Connected: %s (%d devices)", "コントローラー接続済み: %s (%d台)", "연결된 컨트롤러: %s (%d대)"},
    {"通用手柄", "通用手把", "Generic Controller", "汎用コントローラー", "일반 컨트롤러"},
    {"未检测到手柄（可直接使用键鼠游玩；随时插入手柄即可自动识别）", "未檢測到手把（可直接使用鍵鼠遊玩；隨時插入手把即可自動識別）", "No controller detected (keyboard/mouse ready; plug in a controller anytime)", "コントローラー未接続（キーボード/マウス対応・接続で自動認識）", "컨트롤러 없음 (키보드/마우스 사용 가능·연결 시 자동 인식)"},
    {"正在等待按键录入（剩余 %d 秒，按 Esc / 返回键取消）...", "正在等待按鍵錄入（剩餘 %d 秒，按 Esc / 返回鍵取消）...", "Press any button/key (%ds remaining, Esc / Back to cancel)...", "入力待機中（残り %d 秒、Esc / 戻るでキャンセル）...", "입력 대기 중 (%d초 남음, Esc / 뒤로 키로 취소)..."},
    {"点击或按确认键选择右侧任意映射按钮即可重绑（支持手柄、键盘、鼠标五键与滚轮）。", "點擊或按確認鍵選擇右側任意映射按鈕即可重綁（支援手把、鍵盤、滑鼠五鍵與滾輪）。", "Select any button to rebind (supports Gamepad, Keyboard, Mouse buttons & Wheel).", "ボタンを選択してキー・ボタン割り当てを変更できます（マウス・ホイール対応）。", "변경할 매핑 버튼을 선택하세요(컨트롤러, 키보드, 마우스 및 휠 지원)."},
    {"已将「%s」快捷键更新为 %s。", "已將「%s」快捷鍵更新為 %s。", "Updated hotkey \"%s\" to %s.", "ホットキー「%s」を %s に変更しました。", "단축키 '%s'을(를) %s(으)로 변경했습니다."},
    {"已将「%s」的%s映射更新为 %s。", "已將「%s」的%s映射更新為 %s。", "Bound \"%s\" (%s) to %s.", "「%s」の%s割り当てを %s に変更しました。", "'%s'의 %s 매핑을 %s(으)로 변경했습니다."},
    {"快捷键冲突：已与「%s」对调为 %s。", "快捷鍵衝突：已與「%s」對調為 %s。", "Hotkey conflict: swapped with \"%s\" (%s).", "競合のため「%s」と入れ替えました (%s)。", "단축키 충돌: '%s'와(과) %s(으)로 맞교환됨."},
    {"键位冲突：已自动与「%s」对调为 %s。", "鍵位衝突：已自動與「%s」對調為 %s。", "Binding conflict: swapped with \"%s\" (%s).", "競合のため「%s」と入れ替えました (%s)。", "키 충돌: '%s'와(과) %s(으)로 맞교환됨."},
    {"已取消本次按键捕获。", "已取消本次按鍵捕獲。", "Binding capture cancelled.", "入力キャプチャをキャンセルしました。", "입력 캡처가 취소되었습니다."},
    {"等待按键超时（5 秒未操作）。", "等待按鍵逾時（5 秒未操作）。", "Binding capture timed out (5s).", "タイムアウトしました（5秒未入力）。", "입력 대기 시간이 초과되었습니다 (5초)."},
    {"已恢复全局快捷键默认设置。", "已恢復全域快捷鍵預設設定。", "Reset global hotkeys to defaults.", "ホットキーを初期化しました。", "전역 단축키가 초기화되었습니다."},
    {"已应用手柄映射设置。", "已套用手把映射設定。", "Applied controller layout preset.", "コントローラープリセットを適用しました。", "컨트롤러 프리셋이 적용되었습니다."},
    {"已恢复键鼠默认映射。", "已恢復鍵鼠預設映射。", "Reset keyboard & mouse bindings to defaults.", "キーボード/マウス割り当てを初期化しました。", "키보드/마우스 매핑이 초기화되었습니다."},
    {"手柄", "手把", "Controller", "コントローラー", "컨트롤러"},
    {"键鼠", "鍵鼠", "KB/Mouse", "キーボード/マウス", "키보드/마우스"},

    {"手柄预设与摇杆 / 震动参数", "手把預設與搖桿 / 震動參數", "Presets, Sticks & Rumble", "プリセット・スティック・振動", "컨트롤러 프리셋 및 진동/스틱"},
    {"任天堂物理布局（A↔B、X↔Y）", "任天堂物理佈局（A↔B、X↔Y）", "Nintendo Physical (A↔B, X↔Y)", "Nintendo 配置 (A↔B, X↔Y)", "닌텐도 물리 배열 (A↔B, X↔Y)"},
    {"Xbox 字母布局（A=A、B=B）", "Xbox 字母佈局（A=A、B=B）", "Xbox Letter (A=A, B=B)", "Xbox 文字配置 (A=A, B=B)", "Xbox 문자 배열 (A=A, B=B)"},
    {"左摇杆死区", "左搖桿死區", "Left Stick Deadzone", "左スティックデッドゾーン", "왼쪽 스틱 데드존"},
    {"右摇杆死区", "右搖桿死區", "Right Stick Deadzone", "右スティックデッドゾーン", "오른쪽 스틱 데드존"},
    {"扳机触发阈值", "扳機觸發閾值", "Trigger Threshold", "トリガー反応しきい値", "트리거 입력 임계값"},
    {"震动强度", "震動強度", "Rumble Strength", "振動の強さ", "진동 세기"},
    {"启用手柄震动", "啟用手把震動", "Enable Rumble", "コントローラー振動を有効化", "컨트롤러 진동 사용"},
    {"后台保持输入响应", "後台保持輸入響應", "Background Input", "バックグラウンド入力許可", "백그라운드 입력 허용"},

    {"Switch 逻辑键位映射表", "Switch 邏輯鍵位映射表", "Switch Logical Button Mapping", "Switch 論理ボタン割り当て", "Switch 논리 키 매핑"},
    {"Switch 逻辑按键", "Switch 邏輯按鍵", "Switch Input", "Switch ボタン", "Switch 입력"},
    {"手柄映射", "手把映射", "Controller", "コントローラー", "컨트롤러"},
    {"键鼠映射", "鍵鼠映射", "Keyboard / Mouse", "キーボード/マウス", "키보드 / 마우스"},
    {"恢复手柄默认", "恢復手把預設", "Reset Controller", "コントローラー初期化", "컨트롤러 초기화"},
    {"恢复键鼠默认", "恢復鍵鼠預設", "Reset KB / Mouse", "キー/マウス初期化", "키보드/마우스 초기화"},

    {"全局快捷键配置", "全域快捷鍵配置", "Global Hotkeys", "グローバルホットキー", "전역 단축키 설정"},
    {"功能", "功能", "Action", "機能", "기능"},
    {"快捷键", "快捷鍵", "Hotkey", "キー", "단축키"},
    {"恢复默认快捷键", "恢復預設快捷鍵", "Reset Hotkeys", "ホットキー初期化", "단축키 초기화"},
    {"呼出设置菜单", "呼出設定選單", "Toggle Settings Menu", "設定メニュー表示", "설정 메뉴 열기/닫기"},
    {"切换性能 HUD", "切換效能 HUD", "Cycle Performance HUD", "パフォーマンス HUD 切替", "성능 HUD 전환"},
    {"切换快进倍速", "切換快進倍速", "Cycle Fast-Forward", "倍速切替", "빠른 배속 전환"},
    {"切换全屏", "切換全螢幕", "Toggle Fullscreen", "フルスクリーン切替", "전체 화면 전환"},

    {"请按键... %d 秒", "請按鍵... %d 秒", "Press... %ds", "入力... %d秒", "입력... %d초"},
    {"未绑定", "未綁定", "Unbound", "未設定", "미지정"},
    {"鼠标左键", "滑鼠左鍵", "Mouse Left", "マウス左", "마우스 왼쪽"},
    {"鼠标右键", "滑鼠右鍵", "Mouse Right", "マウス右", "마우스 오른쪽"},
    {"鼠标中键", "滑鼠中鍵", "Mouse Middle", "マウス中", "마우스 휠 클릭"},
    {"鼠标侧键 1", "滑鼠側鍵 1", "Mouse X1", "マウス戻る", "마우스 X1"},
    {"鼠标侧键 2", "滑鼠側鍵 2", "Mouse X2", "マウス進む", "마우스 X2"},
    {"滚轮向上", "滾輪向上", "Wheel Up", "ホイール上", "휠 위"},
    {"滚轮向下", "滾輪向下", "Wheel Down", "ホイール下", "휠 아래"},
    {"十字键 ↑", "十字鍵 ↑", "D-Pad ↑", "十字キー ↑", "십자키 ↑"},
    {"十字键 ↓", "十字鍵 ↓", "D-Pad ↓", "十字キー ↓", "십자키 ↓"},
    {"十字键 ←", "十字鍵 ←", "D-Pad ←", "十字キー ←", "십자키 ←"},
    {"十字键 →", "十字鍵 →", "D-Pad →", "十字キー →", "십자키 →"},
    {"左摇杆 ↑", "左搖桿 ↑", "L Stick ↑", "左スティック ↑", "왼쪽 스틱 ↑"},
    {"左摇杆 ↓", "左搖桿 ↓", "L Stick ↓", "左スティック ↓", "왼쪽 스틱 ↓"},
    {"左摇杆 ←", "左搖桿 ←", "L Stick ←", "左スティック ←", "왼쪽 스틱 ←"},
    {"左摇杆 →", "左搖桿 →", "L Stick →", "左スティック →", "왼쪽 스틱 →"},
    {"右摇杆 ↑", "右搖桿 ↑", "R Stick ↑", "右スティック ↑", "오른쪽 스틱 ↑"},
    {"右摇杆 ↓", "右搖桿 ↓", "R Stick ↓", "右スティック ↓", "오른쪽 스틱 ↓"},
    {"右摇杆 ←", "右搖桿 ←", "R Stick ←", "右スティック ←", "오른쪽 스틱 ←"},
    {"右摇杆 →", "右搖桿 →", "R Stick →", "右スティック →", "오른쪽 스틱 →"},
    {"A 键", "A 鍵", "A Button", "A ボタン", "A 버튼"},
    {"B 键", "B 鍵", "B Button", "B ボタン", "B 버튼"},
    {"X 键", "X 鍵", "X Button", "X ボタン", "X 버튼"},
    {"Y 键", "Y 鍵", "Y Button", "Y ボタン", "Y 버튼"},
    {"L 肩键", "L 肩鍵", "L Shoulder", "L ボタン", "L 버튼"},
    {"R 肩键", "R 肩鍵", "R Shoulder", "R ボタン", "R 버튼"},
    {"ZL 扳机", "ZL 扳機", "ZL Trigger", "ZL ボタン", "ZL 트리거"},
    {"ZR 扳机", "ZR 扳機", "ZR Trigger", "ZR ボタン", "ZR 트리거"},
    {"L3（左摇杆按下）", "L3（左搖桿按下）", "L3 (Left Stick Click)", "L3 (左スティック押込)", "L3 (왼쪽 스틱 클릭)"},
    {"R3（右摇杆按下）", "R3（右搖桿按下）", "R3 (Right Stick Click)", "R3 (右スティック押込)", "R3 (오른쪽 스틱 클릭)"},
    {"Plus (+)", "Plus (+)", "Plus (+)", "Plus (+)", "Plus (+)"},
    {"Minus (-)", "Minus (-)", "Minus (-)", "Minus (-)", "Minus (-)"},

    // HUD & Shortcut Footer
    {"帧耗时: %.1f ms (最大 %.0f ms)", "幀耗時: %.1f ms (最大 %.0f ms)", "Frame: %.1f ms (max %.0f ms)", "フレーム: %.1f ms (最大 %.0f ms)", "프레임: %.1f ms (최대 %.0f ms)"},
    {"绘制: %llu 次/帧", "繪製: %llu 次/幀", "Draws: %llu /frame", "描画数: %llu /フレーム", "드로우: %llu /프레임"},
    {"等待: %.2f ms/帧", "等待: %.2f ms/幀", "Wait: %.2f ms/frame", "待機: %.2f ms/フレーム", "대기: %.2f ms/프레임"},
    {"%llux 快进（已静音）", "%llux 快進（已靜音）", "%llux Speed (Muted)", "%llux 早送り (消音中)", "%llux 배속 (음소거)"},
    {"[十字键/左摇杆] 移动   [LB/RB] 切换标签   [%s] 确认   [%s] 返回", "[十字鍵/左搖桿] 移動   [LB/RB] 切換分頁   [%s] 確認   [%s] 返回", "[D-Pad / L-Stick] Move   [LB / RB] Switch Tab   [%s] Confirm   [%s] Back", "[十字キー/左スティック] 移動   [LB/RB] タブ切替   [%s] 決定   [%s] 戻る", "[십자키/왼쪽 스틱] 이동   [LB/RB] 탭 전환   [%s] 확인   [%s] 뒤로"},

    // Local self-build progress & status
    {"正在本地构建游戏代码模块：", "正在本地構建遊戲程式碼模組：", "Building game module locally: ", "モジュールをローカル構築中: ", "게임 모듈 로컬 빌드 중: "},
    {"取消构建", "取消構建", "Cancel Build", "構築キャンセル", "빌드 취소"},
    {"✓ 游戏代码模块本地构建完成！", "✓ 遊戲程式碼模組本地構建完成！", "✓ Game module built successfully!", "✓ ゲームモジュールの構築が完了しました！", "✓ 게임 모듈 빌드가 완료되었습니다!"},
    {"× 本地构建失败：", "× 本地構建失敗：", "× Local build failed: ", "× 構築に失敗しました: ", "× 로컬 빌드 실패: "},
    {"× 本机构建工具链不完整（缺少 lasgen / zig / 13SentinelsRecomp.dll.a）。请确认发布包完整。", "× 本機構建工具鏈不完整（缺少 lasgen / zig / 13SentinelsRecomp.dll.a）。請確認發佈包完整。", "× Local build toolchain incomplete (missing lasgen / zig / 13SentinelsRecomp.dll.a).", "× ビルドツールチェーンが見つかりません (lasgen / zig / implib)。", "× 로컬 빌드 도구가 누락되었습니다 (lasgen / zig / 13SentinelsRecomp.dll.a)."},
    {"提示：本发行包未内置 .nsp/.xci 解包器，请直接选择已解包的 exefs/romfs 文件夹。", "提示：本發佈包未內建 .nsp/.xci 解包器，請直接選擇已解包的 exefs/romfs 資料夾。", "Note: No package extractor is bundled; please select an already-extracted exefs/romfs folder.", "注: パッケージ展開ツールは同梱されていません。展開済みの exefs/romfs フォルダを直接選択してください。", "안내: 패키지 추출 도구가 포함되지 않았습니다. 추출된 exefs/romfs 폴더를 직접 선택하세요."},
};

}  // namespace

extern "C" {

int ui_resolve_lang(void) {
    return settings_resolved_ui_lang();
}

const char *ui_tr_lang(int lang, UiStr id) {
    if (id < 0 || id >= STR_COUNT) return "";
    const Entry &e = k_table[id];
    switch (lang) {
    case UI_LANG_ZH_HANT: return e.zh_hant ? e.zh_hant : e.zh_hans;
    case UI_LANG_EN: return e.en ? e.en : e.zh_hans;
    case UI_LANG_JA: return e.ja ? e.ja : e.en;
    case UI_LANG_KO: return e.ko ? e.ko : e.en;
    default: return e.zh_hans;
    }
}

const char *ui_tr(UiStr id) {
    return ui_tr_lang(ui_resolve_lang(), id);
}

const char *const *ui_lang_selector_labels(int *out_count) {
    static const char *labels[N_UI_LANGS] = {
        "自动 (Auto)", "简体中文", "繁體中文", "English", "日本語", "한국어"
    };
    if (out_count) *out_count = N_UI_LANGS;
    return labels;
}

const char *const *ui_game_lang_labels(int *out_count) {
    static const char *buf[N_GAME_LANGS];
    static int built_lang = -1;
    int rl = ui_resolve_lang();
    if (built_lang != rl) {
        built_lang = rl;
        static const char *const k_auto_lbl[] = {
            "自动（跟随版本/系统）", "自動（跟隨版本/系統）", "Auto (Match Version/OS)", "自動 (バージョン/OS準拠)", "자동 (버전/시스템 따름)"
        };
        int idx = (rl >= 1 && rl <= 5) ? rl - 1 : 0;
        buf[0] = k_auto_lbl[idx];
        buf[1] = "简体中文 (zh-Hans)";
        buf[2] = "繁體中文 (zh-Hant)";
        buf[3] = "한국어 (ko)";
        buf[4] = "日本語 (ja)";
        buf[5] = "English (en-US)";
        buf[6] = "Français (fr)";
        buf[7] = "Deutsch (de)";
        buf[8] = "Italiano (it)";
        buf[9] = "Español (es)";
    }
    if (out_count) *out_count = N_GAME_LANGS;
    return buf;
}

const char *ui_i18n_lang_name(int resolved_lang) {
    switch (resolved_lang) {
    case UI_LANG_ZH_HANS: return "zh-Hans";
    case UI_LANG_ZH_HANT: return "zh-Hant";
    case UI_LANG_EN: return "en";
    case UI_LANG_JA: return "ja";
    case UI_LANG_KO: return "ko";
    default: return "zh-Hans";
    }
}

void ui_i18n_for_each_text(void (*cb)(int lang, const char *utf8, void *user), void *user) {
    if (!cb) return;
    int ui_n = 0;
    const char *const *ui_lbls = ui_lang_selector_labels(&ui_n);
    for (int i = 0; i < ui_n; i++) cb(0, ui_lbls[i], user);

    static const char *const k_extra_gl[] = {
        "自动（跟随版本/系统）", "自動（跟隨版本/系統）", "Auto (Match Version/OS)",
        "自動 (バージョン/OS準拠)", "자동 (버전/시스템 따름)",
        "简体中文 (zh-Hans)", "繁體中文 (zh-Hant)", "한국어 (ko)", "日本語 (ja)", "English (en-US)",
        "Français (fr)", "Deutsch (de)", "Italiano (it)", "Español (es)"
    };
    for (const char *s : k_extra_gl) cb(0, s, user);

    for (int lang = UI_LANG_ZH_HANS; lang <= UI_LANG_KO; lang++) {
        for (int id = 0; id < STR_COUNT; id++) {
            const char *s = ui_tr_lang(lang, (UiStr)id);
            if (s && *s) cb(lang, s, user);
        }
    }
}

const char *ui_i18n_all_languages_corpus(void) {
    static std::string all_cache;
    if (all_cache.empty()) {
        all_cache += "自动简体中文繁體中文한국어日本語English Auto";
        all_cache += "±·×—‘’“”•…‹›←↑→↓↔▶▸◀✓、。〈〉《》「」『』【】！＃＆（）＊＋，－／：；＝？＠～￥※";
        ui_i18n_for_each_text([](int, const char *utf8, void *user) {
            std::string *out = (std::string *)user;
            *out += utf8;
            *out += '\n';
        }, &all_cache);
    }
    return all_cache.c_str();
}

const char *ui_i18n_corpus_for_lang(int resolved_lang) {
    (void)resolved_lang;
    return ui_i18n_all_languages_corpus();
}

}  // extern "C"
