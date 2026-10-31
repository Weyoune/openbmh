// SPDX-License-Identifier: GPL-3.0-only
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <gdiplus.h>
#include "obdeddi/logo_win32.hpp"
#include "obdeddi/logo_transform_win32.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
namespace bmh::obdeddi {
namespace {
struct Dialog {
 HWND window=nullptr,owner=nullptr,edits[5]{},after=nullptr,apply=nullptr;HFONT font=nullptr;
 Document* document=nullptr;size_t club=0;int dpi=96;bool done=false,accepted=false,mirrorX=false,mirrorY=false;
 ULONG_PTR token=0;std::unique_ptr<Gdiplus::Bitmap> source;std::array<uint32_t,1444> before{},preview{},rgba{};
 int px(int n)const{return MulDiv(n,dpi,96);}
 HWND child(const wchar_t* cls,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h){auto c=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;}
 std::wstring value(int n){wchar_t text[64]{};GetWindowTextW(edits[n],text,64);return text;}
 void set(int n,int v){SetWindowTextW(edits[n],std::to_wstring(v).c_str());}
 void whole(){if(!source)return;set(0,0);set(1,0);set(2,int(source->GetWidth()));set(3,int(source->GetHeight()));}
 void update(){if(!source)return;try{int crop[4];for(int i=0;i<4;++i){auto s=value(i);size_t used=0;crop[i]=std::stoi(s,&used);if(used!=s.size())throw std::runtime_error("number");}auto angleText=value(4);std::replace(angleText.begin(),angleText.end(),L',',L'.');size_t used=0;float angle=std::stof(angleText,&used);if(used!=angleText.size()||!std::isfinite(angle)||angle < -360||angle>360)throw std::runtime_error("angle");
 if(crop[0]<0||crop[1]<0||crop[2]<=0||crop[3]<=0||int64_t(crop[0])+crop[2]>source->GetWidth()||int64_t(crop[1])+crop[3]>source->GetHeight())throw std::runtime_error("crop");
 rgba=transform_logo(*source,crop[0],crop[1],crop[2],crop[3],angle,mirrorX,mirrorY);preview=document->preview_logo(rgba);InvalidateRect(after,nullptr,TRUE);EnableWindow(apply,TRUE);
 }catch(...){EnableWindow(apply,FALSE);MessageBoxW(window,L"Bitte einen gültigen Ausschnitt innerhalb des Bildes und einen Winkel zwischen -360 und 360 eingeben.",L"Vereinslogo",MB_OK|MB_ICONWARNING);}}
 void import(){wchar_t path[32768]{};OPENFILENAMEW f{sizeof(f)};f.hwndOwner=window;f.lpstrFilter=L"Bilder (PNG, JPEG, BMP)\0*.png;*.jpg;*.jpeg;*.bmp\0\0";f.lpstrFile=path;f.nMaxFile=32768;f.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetOpenFileNameW(&f))return;auto image=std::unique_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromFile(path,FALSE));if(!image||image->GetLastStatus()!=Gdiplus::Ok||image->GetWidth()==0||image->GetHeight()==0||image->GetWidth()>16384||image->GetHeight()>16384||uint64_t(image->GetWidth())*image->GetHeight()>64000000){MessageBoxW(window,L"Das Bild konnte nicht geladen werden oder ist zu groß.",L"Vereinslogo",MB_OK|MB_ICONERROR);return;}source=std::move(image);mirrorX=mirrorY=false;set(4,0);whole();update();}
};
void draw(DRAWITEMSTRUCT* item,const std::array<uint32_t,1444>& pixels){FillRect(item->hDC,&item->rcItem,GetSysColorBrush(COLOR_WINDOW));BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=38;info.bmiHeader.biHeight=-38;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;int size=std::min(int(item->rcItem.right-item->rcItem.left),int(item->rcItem.bottom-item->rcItem.top));int x=item->rcItem.left+(int(item->rcItem.right-item->rcItem.left)-size)/2,y=item->rcItem.top+(int(item->rcItem.bottom-item->rcItem.top)-size)/2;SetStretchBltMode(item->hDC,COLORONCOLOR);StretchDIBits(item->hDC,x,y,size,size,0,0,38,38,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);}
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l){auto* d=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){d=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(d));d->window=window;}
 if(!d)return DefWindowProcW(window,message,w,l);
 if(message==WM_DRAWITEM){auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);draw(item,item->CtlID==51?d->before:d->preview);return TRUE;}
 if(message==WM_COMMAND){switch(LOWORD(w)){case 10:d->import();break;case 11:d->whole();d->update();break;case 12:d->mirrorX=!d->mirrorX;d->update();break;case 13:d->mirrorY=!d->mirrorY;d->update();break;case 14:d->update();break;case IDOK:if(d->source){d->update();if(IsWindowEnabled(d->apply)){try{d->document->set_logo(d->club,d->rgba);d->accepted=true;SendMessageW(window,WM_CLOSE,0,0);}catch(const std::exception&){MessageBoxW(window,L"Das Logo konnte nicht übernommen werden. Bitte die Wappenzuordnung prüfen.",L"Vereinslogo",MB_OK|MB_ICONERROR);}}}break;case IDCANCEL:SendMessageW(window,WM_CLOSE,0,0);break;}return 0;}
 if(message==WM_CLOSE){DestroyWindow(window);return 0;}if(message==WM_DESTROY){d->done=true;return 0;}return DefWindowProcW(window,message,w,l);}
}
bool edit_logo(HWND owner,Document& document,size_t club){Dialog d;d.owner=owner;d.document=&document;d.club=club;d.before=document.logo(club);d.preview=d.before;HDC dc=GetDC(owner);d.dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(owner,dc);Gdiplus::GdiplusStartupInput startup;if(Gdiplus::GdiplusStartup(&d.token,&startup,nullptr)!=Gdiplus::Ok)throw FormatError("Bildimport kann nicht gestartet werden.");
 d.font=CreateFontW(-d.px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);wc.lpszClassName=L"obdeddi.logo";RegisterClassW(&wc);
 CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,L"obdeddi.logo",L"Vereinslogo",WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,d.px(560),d.px(580),owner,nullptr,wc.hInstance,&d);
 d.child(L"STATIC",L"Bisher",0,0,18,12,230,24);d.child(L"STATIC",L"Vorschau im Originalformat",0,0,286,12,250,24);d.child(L"STATIC",L"",SS_OWNERDRAW|WS_BORDER,51,28,42,190,190);d.after=d.child(L"STATIC",L"",SS_OWNERDRAW|WS_BORDER,52,310,42,190,190);
 d.child(L"BUTTON",L"Bild importieren…",WS_TABSTOP,10,18,247,220,28);d.child(L"BUTTON",L"Ganzes Bild",WS_TABSTOP,11,286,247,250,28);
 const wchar_t* labels[]={L"Links",L"Oben",L"Breite",L"Höhe"};for(int i=0;i<4;++i){d.child(L"STATIC",labels[i],0,0,18+i*130,290,120,22);d.edits[i]=d.child(L"EDIT",L"0",WS_TABSTOP|ES_AUTOHSCROLL,30+i,18+i*130,315,118,24);}
 d.child(L"STATIC",L"Drehung (Grad)",0,0,18,359,120,22);d.edits[4]=d.child(L"EDIT",L"0",WS_TABSTOP|ES_AUTOHSCROLL,34,150,353,86,24);d.child(L"BUTTON",L"Horizontal spiegeln",WS_TABSTOP,12,260,350,276,28);d.child(L"BUTTON",L"Vertikal spiegeln",WS_TABSTOP,13,260,386,276,28);d.child(L"BUTTON",L"Vorschau aktualisieren",WS_TABSTOP,14,18,395,220,28);
 d.child(L"STATIC",L"38 × 38 Pixel, Originalpalette, heller Original-Hintergrund. Speichern erfolgt anschließend im Hauptfenster mit Backup.",0,0,18,433,518,38);d.apply=d.child(L"BUTTON",L"Übernehmen",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,260,478,132,28);d.child(L"BUTTON",L"Abbrechen",WS_TABSTOP,IDCANCEL,402,478,134,28);EnableWindow(d.apply,FALSE);
 EnableWindow(owner,FALSE);ShowWindow(d.window,SW_SHOW);UpdateWindow(d.window);MSG m{};while(!d.done&&GetMessageW(&m,nullptr,0,0)>0){if(!IsDialogMessageW(d.window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}EnableWindow(owner,TRUE);SetActiveWindow(owner);d.source.reset();DeleteObject(d.font);Gdiplus::GdiplusShutdown(d.token);return d.accepted;
}
}
