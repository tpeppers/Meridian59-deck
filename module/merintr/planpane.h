// The PLAN tab: fleet intents as a Spells-like list, served by the harness plan desk.
#ifndef M59_PLAN_PANE_H
#define M59_PLAN_PANE_H
#include <string>
#include <vector>
namespace M59InventoryIntent { struct Item; }
void PlanPaneCreate(HWND parent, const std::string& root);
void PlanPaneDestroy();
void PlanPaneShow();
void PlanPaneHide();
void PlanPaneResize(int top, int width, int height);
void PlanPaneInventory(const std::vector<M59InventoryIntent::Item>& items);
void PlanPaneNoteIcon(const char* name, ID icon);
// True while the harness says Keeper holds this character's controls (the proxy gate).
// mermain.c refuses movement, combat and item actions locally while it is.
bool PlanPaneLocked();
bool PlanPaneVetoes(int action);
// The tab button's caption and whether it should read as "held by Keeper".
const char* PlanPaneButtonLabel(bool* locked);
// Typed commands: "plan [entry] [key=value ...]" and "fs <fleetscript line>".
void CommandPlan(char* args);
void CommandFleetScript(char* args);
// Provided by clientcontext.c: the eight identity fields the DUM pane binds to.
std::string ClientContextBinding();
#endif
