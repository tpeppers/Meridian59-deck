// Headless parser and GDI badge test; no Meridian connection or GUI window.
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <iostream>
#include "inventoryintent.h"
int main() {
  using namespace M59InventoryIntent;
  const auto now=Now();
  const std::string header="M59SELL\t1\t"+std::to_string(now)+"\t"+std::to_string(GetCurrentProcessId())+"\tfixture\t17\n";
  const std::string row="I\t31\tBread%20%26%20Cheese\tsell\n";
  std::map<unsigned int,Mark> result;std::uint64_t stamp=0;
  assert(Parse(header+row+"END\n","fixture",17,now,result,stamp));
  assert(result.size()==1&&result[31].state=="sell");
  assert(!Parse(header+row+"END\n","other",17,now,result,stamp));
  assert(!Parse(header+row+"END\n","fixture",18,now,result,stamp));
  assert(!Parse(header+row+"END\n","fixture",17,now+7000,result,stamp));
  assert(!Parse(header+row+row+"END\n","fixture",17,now,result,stamp));
  assert(!Parse(header+row+"END\ntrailing\n","fixture",17,now,result,stamp));
  assert(result.size()==1);
  assert(Encode("Bread & Cheese")=="Bread%20%26%20Cheese");
  HDC dc=CreateCompatibleDC(NULL);
  BITMAPINFO bi={};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=40;
  bi.bmiHeader.biHeight=-40;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
  void* pixels=NULL;HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,NULL,0);
  auto previous=SelectObject(dc,bitmap);RECT area={0,0,40,40};
  marks=result;mark_player=17;mark_time=now;
  Draw(dc,area,17,31,"Bread & Cheese");assert(GetPixel(dc,20,20)==RGB(70,235,110));
  marks[31].state="keep";Draw(dc,area,17,31,"Bread & Cheese");assert(GetPixel(dc,20,20)==RGB(120,185,255));
  marks[31].state="blocked";Draw(dc,area,17,31,"Bread & Cheese");assert(GetPixel(dc,20,20)==RGB(255,190,55));
  mark_time=now-7000;Draw(dc,area,17,31,"Bread & Cheese");assert(GetPixel(dc,20,20)==RGB(155,160,145));
  SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
  std::cout<<"native inventory parser and badge tests passed\n";
}
