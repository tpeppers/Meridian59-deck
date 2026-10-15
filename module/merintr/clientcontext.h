#ifndef M59_CLIENT_CONTEXT_H
#define M59_CLIENT_CONTEXT_H
#include <vector>
namespace M59InventoryIntent { struct Item; }
bool ClientContextEnabled();
bool ClientContextGroup(StatGroup group);
void ClientContextCreate(HWND parent);
void ClientContextDestroy();
void ClientContextResize();
void ClientContextShow(StatGroup group);
void ClientContextHide();
void ClientContextConditionDraw(HDC dc,const RECT& area,unsigned int id,const char* name);
void ClientContextInventory(const std::vector<M59InventoryIntent::Item>& items);
void ClientContextStat(StatGroup group,const Statistic* stat);
#endif
