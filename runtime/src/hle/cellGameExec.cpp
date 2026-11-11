#include "common.h"

/* cellGameExec: the application-launch and PlayStation Home side of cellGame.
 *
 * This title imports exactly one function from the module, and only one is
 * implemented here:
 *
 *   cellGameExec::cellGameGetHomeDataExportPath (NID 0x59B1EDE1)
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGameExec.cpp:20-31.
 * RPCS3 validates the pointer and then returns CELL_GAME_ERROR_NOAPP for
 * every call, with the comment "PlayStation home is defunct". That is a
 * MEASURED fact, not a shortcut: there is no Home data export path on any
 * console today because the service no longer exists, so there is no path
 * this runtime could hand back. The previous state of this tree was worse
 * than either answer: the import resolved nowhere, the generic handler wrote
 * r3 = 0 (CELL_OK), and the title was told a path had been written when the
 * buffer still held whatever was there before.
 *
 * The other nine exports of the module (cellGameSetExitParam, GetHomePath,
 * GetHomeDataImportPath, GetHomeLaunchOptionPath, ExecGame, DeleteGame,
 * GetBootGameInfo, GetExitGameInfo, GetList) are NOT written: this title does
 * not import them, and a handler with no caller is a handler nobody has
 * verified. They stay unresolved, which the runtime logs by name.
 */

namespace hle {

namespace {

/* refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.h:8-29. */
constexpr int32_t ERR_PARAM = static_cast<int32_t>(0x8002cb07);
constexpr int32_t ERR_NOAPP = static_cast<int32_t>(0x8002cb08);

}  // namespace

/* cellGameGetHomeDataExportPath(exportPath)
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGameExec.cpp:20-31. */
HLE_FUNC(cellGameExec, cellGameGetHomeDataExportPath) {
    const uint32_t export_path = a32(ctx, 0);
    if (!export_path) {
        LOG_WARN("cellGameExec", "cellGameGetHomeDataExportPath(NULL) -> CELL_GAME_ERROR_PARAM");
        return ret(ctx, ERR_PARAM);
    }
    LOG_WARN("cellGameExec",
             "cellGameGetHomeDataExportPath(0x%08x) -> CELL_GAME_ERROR_NOAPP: PlayStation Home is defunct, "
             "there is no export path to give",
             export_path);
    ret(ctx, ERR_NOAPP);
}

}  // namespace hle
