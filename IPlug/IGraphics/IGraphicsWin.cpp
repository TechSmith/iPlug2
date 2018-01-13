
#include <Shlobj.h>
#include <commctrl.h>

#include "IGraphicsWin.h"
#include "Log.h"

#include <wininet.h>

#pragma warning(disable:4244) // Pointer size cast mismatch.
#pragma warning(disable:4312) // Pointer size cast mismatch.
#pragma warning(disable:4311) // Pointer size cast mismatch.

static int nWndClassReg = 0;
static const char* wndClassName = "IPlugWndClass";
static double sFPS = 0.0;

#define PARAM_EDIT_ID 99
#define IPLUG_TIMER_ID 2

inline IMouseMod GetMouseMod(WPARAM wParam)
{
  return IMouseMod((wParam & MK_LBUTTON), 
                   (wParam & MK_RBUTTON),
                   (wParam & MK_SHIFT), 
                   (wParam & MK_CONTROL), 
                   
#ifdef AAX_API
                   GetAsyncKeyState(VK_MENU) < 0
#else
                   GetKeyState(VK_MENU) < 0
#endif
                   );
}

// static
LRESULT CALLBACK IGraphicsWin::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_CREATE)
  {
    LPCREATESTRUCT lpcs = (LPCREATESTRUCT) lParam;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LPARAM) (lpcs->lpCreateParams));
    int mSec = int(1000.0 / sFPS);
    SetTimer(hWnd, IPLUG_TIMER_ID, mSec, NULL);
    SetFocus(hWnd); // gets scroll wheel working straight away
    DragAcceptFiles(hWnd, true);
    return 0;
  }

  IGraphicsWin* pGraphics = (IGraphicsWin*) GetWindowLongPtr(hWnd, GWLP_USERDATA);
  char txt[MAX_PARAM_LEN];
  double v;

  if (!pGraphics || hWnd != pGraphics->mPlugWnd)
  {
    return DefWindowProc(hWnd, msg, wParam, lParam);
  }
  if (pGraphics->mParamEditWnd && pGraphics->mParamEditMsg == kEditing)
  {
    if (msg == WM_RBUTTONDOWN || (msg == WM_LBUTTONDOWN))
    {
      pGraphics->mParamEditMsg = kCancel;
      return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
  }

  switch (msg)
  {

    case WM_TIMER:
    {
      if (wParam == IPLUG_TIMER_ID)
      {

        if (pGraphics->mParamEditWnd && pGraphics->mParamEditMsg != kNone)
        {
          switch (pGraphics->mParamEditMsg)
          {
            case kCommit:
            {
              SendMessage(pGraphics->mParamEditWnd, WM_GETTEXT, MAX_PARAM_LEN, (LPARAM) txt);

              if(pGraphics->mEdParam)
              {
                IParam::EParamType type = pGraphics->mEdParam->Type();

                if ( type == IParam::kTypeEnum || type == IParam::kTypeBool)
                {
                  int vi = 0;
                  pGraphics->mEdParam->MapDisplayText(txt, &vi);
                  v = (double) vi;
                }
                else
                {
                  v = atof(txt);
                  if (pGraphics->mEdParam->GetDisplayIsNegated())
                  {
                    v = -v;
                  }
                }
                pGraphics->mEdControl->SetValueFromUserInput(pGraphics->mEdParam->GetNormalized(v));
              }
              else
              {
                pGraphics->mEdControl->TextFromTextEntry(txt);
              }
              // Fall through.
            }
            case kCancel:
            {
              SetWindowLongPtr(pGraphics->mParamEditWnd, GWLP_WNDPROC, (LPARAM) pGraphics->mDefEditProc);
              DestroyWindow(pGraphics->mParamEditWnd);
              pGraphics->mParamEditWnd = 0;
              pGraphics->mEdParam = 0;
              pGraphics->mEdControl = 0;
              pGraphics->mDefEditProc = 0;
            }
            break;
          }
          pGraphics->mParamEditMsg = kNone;
          return 0; // TODO: check this!
        }

        IRECT dirtyR;
        if (pGraphics->IsDirty(dirtyR))
        {
          RECT r = { dirtyR.L, dirtyR.T, dirtyR.R, dirtyR.B };

          InvalidateRect(hWnd, &r, FALSE);

          if (pGraphics->mParamEditWnd)
          {
            IRECT notDirtyR = pGraphics->mEdControl->GetRECT();
            RECT r2 = { notDirtyR.L, notDirtyR.T, notDirtyR.R, notDirtyR.B };
            ValidateRect(hWnd, &r2); // make sure we dont redraw the edit box area
            UpdateWindow(hWnd);
            pGraphics->mParamEditMsg = kUpdate;
          }
          else
          {
            UpdateWindow(hWnd);
          }
        }
      }
      return 0;
    }

    case WM_RBUTTONDOWN:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
      pGraphics->HideTooltip();
      if (pGraphics->mParamEditWnd)
      {
        pGraphics->mParamEditMsg = kCommit;
        return 0;
      }
      SetFocus(hWnd); // Added to get keyboard focus again when user clicks in window
      SetCapture(hWnd);
      pGraphics->OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam));
      return 0;

    case WM_MOUSEMOVE:
    {
      if (!(wParam & (MK_LBUTTON | MK_RBUTTON)))
      {
        if (pGraphics->OnMouseOver(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam)))
        {
          TRACKMOUSEEVENT eventTrack = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, HOVER_DEFAULT };
          if (pGraphics->TooltipsEnabled()) 
          {
            int c = pGraphics->GetMouseOver();
            if (c != pGraphics->mTooltipIdx) 
            {
              if (c >= 0) eventTrack.dwFlags |= TME_HOVER;
              pGraphics->mTooltipIdx = c;
              pGraphics->HideTooltip();
            }
          }

          TrackMouseEvent(&eventTrack);
        }
      }
      else if (GetCapture() == hWnd && !pGraphics->mParamEditWnd)
      {
        pGraphics->OnMouseDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam));
      }

      return 0;
    }
    case WM_MOUSEHOVER: 
    {
      pGraphics->ShowTooltip();
		  return 0;
    }
    case WM_MOUSELEAVE:
    {
      pGraphics->HideTooltip();
      pGraphics->OnMouseOut();
      return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    {
      ReleaseCapture();
      pGraphics->OnMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam));
      return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
      if (pGraphics->OnMouseDblClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &GetMouseMod(wParam)))
      {
        SetCapture(hWnd);
      }
      return 0;
    }
    case WM_MOUSEWHEEL:
    {

      if (pGraphics->mParamEditWnd)
      {
        pGraphics->mParamEditMsg = kCancel;
        return 0;
      }
      else
      {
        int d = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        RECT r;
        GetWindowRect(hWnd, &r);
        pGraphics->OnMouseWheel(x - r.left, y - r.top, &GetMouseMod(wParam), d);
        return 0;
      }
    }

    case WM_KEYDOWN:
    {
      bool handle = true;
      int key;

      if (wParam == VK_SPACE) key = KEY_SPACE;
      else if (wParam == VK_UP) key = KEY_UPARROW;
      else if (wParam == VK_DOWN) key = KEY_DOWNARROW;
      else if (wParam == VK_LEFT) key = KEY_LEFTARROW;
      else if (wParam == VK_RIGHT) key = KEY_RIGHTARROW;
      else if (wParam >= '0' && wParam <= '9') key = KEY_DIGIT_0+wParam-'0';
      else if (wParam >= 'A' && wParam <= 'Z') key = KEY_ALPHA_A+wParam-'A';
      else if (wParam >= 'a' && wParam <= 'z') key = KEY_ALPHA_A+wParam-'a';
      else handle = false;

      if (handle)
      {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hWnd, &p);
        handle = pGraphics->OnKeyDown(p.x, p.y, key);
      }

      if (!handle)
      {
        HWND rootHWnd = GetAncestor( hWnd, GA_ROOT);
        SendMessage(rootHWnd, WM_KEYDOWN, wParam, lParam);
        return DefWindowProc(hWnd, msg, wParam, lParam);
      }
      else
        return 0;
    }
    case WM_KEYUP:
    {
      HWND rootHWnd = GetAncestor(hWnd, GA_ROOT);
      SendMessage(rootHWnd, msg, wParam, lParam);
      return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    case WM_PAINT:
    {
      RECT r;
      if (GetUpdateRect(hWnd, &r, FALSE))
      {
        const IRECT ir(r.left, r.top, r.right, r.bottom);
        pGraphics->Draw(ir);
      }
      return 0;
    }

    case WM_CTLCOLOREDIT:
    {
      if(!pGraphics->mEdControl)
        return 0;

      IText& text = pGraphics->mEdControl->GetText();
      HDC dc = (HDC) wParam;
      SetBkColor(dc, RGB(text.mTextEntryBGColor.R, text.mTextEntryBGColor.G, text.mTextEntryBGColor.B));
      SetTextColor(dc, RGB(text.mTextEntryFGColor.R, text.mTextEntryFGColor.G, text.mTextEntryFGColor.B));
      SetBkMode(dc, OPAQUE);
      return (BOOL) GetStockObject(DC_BRUSH);
    }
    case WM_DROPFILES:
    {
      HDROP hdrop = (HDROP)wParam;
      
      char pathToFile[1025];
      DragQueryFile(hdrop, 0, pathToFile, 1024);
      
      POINT point;
      DragQueryPoint(hdrop, &point);
      
      pGraphics->OnDrop(pathToFile, point.x, point.y);
      
      return 0;
    }
    case WM_CLOSE:
    {
      pGraphics->CloseWindow();
      return 0;
    }
    case WM_SETFOCUS:
    {
      return 0;
    }
    case WM_KILLFOCUS:
    {
      return 0;
    }
  }
  return DefWindowProc(hWnd, msg, wParam, lParam);
}

// static
LRESULT CALLBACK IGraphicsWin::ParamEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  IGraphicsWin* pGraphics = (IGraphicsWin*) GetWindowLongPtr(GetParent(hWnd), GWLP_USERDATA);

  if (pGraphics && pGraphics->mParamEditWnd && pGraphics->mParamEditWnd == hWnd)
  {
    pGraphics->HideTooltip();

    switch (msg)
    {
      case WM_CHAR:
      {
        // limit to numbers for text entry on appropriate parameters
        if(pGraphics->mEdParam)
        {
          char c = wParam;

          if(c == 0x08) break; // backspace

          switch ( pGraphics->mEdParam->Type() )
          {
            case IParam::kTypeEnum:
            case IParam::kTypeInt:
            case IParam::kTypeBool:
              if (c >= '0' && c <= '9') break;
              else if (c == '-') break;
              else if (c == '+') break;
              else return 0;
            case IParam::kTypeDouble:
              if (c >= '0' && c <= '9') break;
              else if (c == '-') break;
              else if (c == '+') break;
              else if (c == '.') break;
              else return 0;
            default:
              break;
          }
        }
        break;
      }
      case WM_KEYDOWN:
      {
        if (wParam == VK_RETURN)
        {
          pGraphics->mParamEditMsg = kCommit;
          return 0;
        }
        else if (wParam == VK_ESCAPE)
        {
          pGraphics->mParamEditMsg = kCancel;
          return 0;
        }
        break;
      }
      case WM_SETFOCUS:
      {
        pGraphics->mParamEditMsg = kEditing;
        break;
      }
      case WM_KILLFOCUS:
      {
        pGraphics->mParamEditMsg = kCommit;
        break;
      }
      // handle WM_GETDLGCODE so that we can say that we want the return key message
      //  (normally single line edit boxes don't get sent return key messages)
      case WM_GETDLGCODE:
      {
        if (pGraphics->mEdParam) break;
        LPARAM lres;
        // find out if the original control wants it
        lres = CallWindowProc(pGraphics->mDefEditProc, hWnd, WM_GETDLGCODE, wParam, lParam);
        // add in that we want it if it is a return keydown
        if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN  &&  wParam == VK_RETURN)
        {
          lres |= DLGC_WANTMESSAGE;
        }
        return lres;
      }
      case WM_COMMAND:
      {
        switch HIWORD(wParam)
        {
          case CBN_SELCHANGE:
          {
            if (pGraphics->mParamEditWnd)
            {
              pGraphics->mParamEditMsg = kCommit;
              return 0;
            }
          }

        }
        break;  // Else let the default proc handle it.
      }
    }
    return CallWindowProc(pGraphics->mDefEditProc, hWnd, msg, wParam, lParam);
  }
  return DefWindowProc(hWnd, msg, wParam, lParam);
}

IGraphicsWin::IGraphicsWin(IPlugBaseGraphics& plug, int w, int h, int fps)
  : IGRAPHICS_DRAW_CLASS(plug, w, h, fps)
{}

IGraphicsWin::~IGraphicsWin()
{
  CloseWindow();
  FREE_NULL(mCustomColorStorage);
}

void GetWindowSize(HWND pWnd, int* pW, int* pH)
{
  if (pWnd)
  {
    RECT r;
    GetWindowRect(pWnd, &r);
    *pW = r.right - r.left;
    *pH = r.bottom - r.top;
  }
  else
  {
    *pW = *pH = 0;
  }
}

bool IsChildWindow(HWND pWnd)
{
  if (pWnd)
  {
    int style = GetWindowLong(pWnd, GWL_STYLE);
    int exStyle = GetWindowLong(pWnd, GWL_EXSTYLE);
    return ((style & WS_CHILD) && !(exStyle & WS_EX_MDICHILD));
  }
  return false;
}

void IGraphicsWin::ForceEndUserEdit()
{
  mParamEditMsg = kCancel;
}

#define SETPOS_FLAGS SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE

void IGraphicsWin::Resize(int w, int h, double scale)
{
  if (w == Width() && h == Height()) return;

  int dw = w - Width(), dh = h - Height();
  IGraphics::Resize(w, h, scale);

  if (WindowIsOpen())
  {
    HWND pParent = 0, pGrandparent = 0;
    int plugW = 0, plugH = 0, parentW = 0, parentH = 0, grandparentW = 0, grandparentH = 0;
    GetWindowSize(mPlugWnd, &plugW, &plugH);

    if (IsChildWindow(mPlugWnd))
    {
      pParent = GetParent(mPlugWnd);
      GetWindowSize(pParent, &parentW, &parentH);

      if (IsChildWindow(pParent))
      {
        pGrandparent = GetParent(pParent);
        GetWindowSize(pGrandparent, &grandparentW, &grandparentH);
      }
    }

    SetWindowPos(mPlugWnd, 0, 0, 0, plugW + dw, plugH + dh, SETPOS_FLAGS);

    // don't want to touch the host window in VST3
    if(mPlug.GetAPI() != kAPIVST3)
    {
      if(pParent)
      {
        SetWindowPos(pParent, 0, 0, 0, parentW + dw, parentH + dh, SETPOS_FLAGS);
      }

      if(pGrandparent)
      {
        SetWindowPos(pGrandparent, 0, 0, 0, grandparentW + dw, grandparentH + dh, SETPOS_FLAGS);
      }
    }

    RECT r = { 0, 0, Width(), Height() };
    InvalidateRect(mPlugWnd, &r, FALSE);
  }
}

void IGraphicsWin::HideMouseCursor()
{
  if (!mCursorHidden)
  {
    POINT p;
    GetCursorPos(&p);

    mHiddenMousePointX = p.x;
    mHiddenMousePointY = p.y;

    ShowCursor(false);
    mCursorHidden=true;
  }
}

void IGraphicsWin::ShowMouseCursor()
{
  if (mCursorHidden)
  {
    SetCursorPos(mHiddenMousePointX, mHiddenMousePointY);
    ShowCursor(true);
    mCursorHidden=false;
  }
}

int IGraphicsWin::ShowMessageBox(const char* text, const char* caption, int type)
{
  return MessageBox(GetMainWnd(), text, caption, type);
}

void IGraphicsWin::DrawScreen(const IRECT& rect)
{
  PAINTSTRUCT ps;
  HWND hWnd = (HWND) GetWindow();
  HDC dc = BeginPaint(hWnd, &ps);
  BitBlt(dc, rect.L, rect.T, rect.W(), rect.H(), (HDC) GetPlatformContext(), rect.L, rect.T, SRCCOPY);
  EndPaint(hWnd, &ps);
}

void* IGraphicsWin::OpenWindow(void* pParentWnd)
{
  int x = 0, y = 0, w = Width(), h = Height();
  mParentWnd = (HWND) pParentWnd;

  if (mPlugWnd)
  {
    RECT pR, cR;
    GetWindowRect((HWND) pParentWnd, &pR);
    GetWindowRect(mPlugWnd, &cR);
    CloseWindow();
    x = cR.left - pR.left;
    y = cR.top - pR.top;
    w = cR.right - cR.left;
    h = cR.bottom - cR.top;
  }

  if (nWndClassReg++ == 0)
  {
    WNDCLASS wndClass = { CS_DBLCLKS, WndProc, 0, 0, mHInstance, 0, LoadCursor(NULL, IDC_ARROW), 0, 0, wndClassName };
    RegisterClass(&wndClass);
  }

  sFPS = FPS();
  mPlugWnd = CreateWindow(wndClassName, "IPlug", WS_CHILD | WS_VISIBLE, // | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                          x, y, w, h, (HWND) pParentWnd, 0, mHInstance, this);
  //SetWindowLong(mPlugWnd, GWL_USERDATA, (LPARAM) this);

  if (!mPlugWnd && --nWndClassReg == 0)
  {
    UnregisterClass(wndClassName, mHInstance);
  }
  else
  {
    SetAllControlsDirty();
  }

  if (mPlugWnd && TooltipsEnabled())
  {
    bool ok = false;
    static const INITCOMMONCONTROLSEX iccex = { sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES };

    if (InitCommonControlsEx(&iccex))
    {
      mTooltipWnd = CreateWindowEx(0, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, mPlugWnd, NULL, mHInstance, NULL);
      if (mTooltipWnd)
      {
        SetWindowPos(mTooltipWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        TOOLINFO ti = { TTTOOLINFOA_V2_SIZE, TTF_IDISHWND | TTF_SUBCLASS, mPlugWnd, (UINT_PTR)mPlugWnd };
        ti.lpszText = (LPTSTR)NULL;
        SendMessage(mTooltipWnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
        ok = true;
      }
    }

    if (!ok) EnableTooltips(ok);
  }

  return mPlugWnd;
}

void GetWndClassName(HWND hWnd, WDL_String* pStr)
{
  char cStr[MAX_CLASSNAME_LEN];
  cStr[0] = '\0';
  GetClassName(hWnd, cStr, MAX_CLASSNAME_LEN);
  pStr->Set(cStr);
}

BOOL CALLBACK IGraphicsWin::FindMainWindow(HWND hWnd, LPARAM lParam)
{
  IGraphicsWin* pGraphics = (IGraphicsWin*) lParam;
  if (pGraphics)
  {
    DWORD wPID;
    GetWindowThreadProcessId(hWnd, &wPID);
    WDL_String str;
    GetWndClassName(hWnd, &str);
    if (wPID == pGraphics->mPID && !strcmp(str.Get(), pGraphics->mMainWndClassName.Get()))
    {
      pGraphics->mMainWnd = hWnd;
      return FALSE;   // Stop enumerating.
    }
  }
  return TRUE;
}

HWND IGraphicsWin::GetMainWnd()
{
  if (!mMainWnd)
  {
    if (mParentWnd)
    {
      HWND parentWnd = mParentWnd;
      while (parentWnd)
      {
        mMainWnd = parentWnd;
        parentWnd = GetParent(mMainWnd);
      }
      GetWndClassName(mMainWnd, &mMainWndClassName);
    }
    else if (CSTR_NOT_EMPTY(mMainWndClassName.Get()))
    {
      mPID = GetCurrentProcessId();
      EnumWindows(FindMainWindow, (LPARAM) this);
    }
  }
  return mMainWnd;
}

IRECT IGraphicsWin::GetWindowRECT()
{
  if (mPlugWnd)
  {
    RECT r;
    GetWindowRect(mPlugWnd, &r);
    r.right -= TOOLWIN_BORDER_W;
    r.bottom -= TOOLWIN_BORDER_H;
    return IRECT(r.left, r.top, r.right, r.bottom);
  }
  return IRECT();
}

void IGraphicsWin::SetWindowTitle(const char* str)
{
  SetWindowText(mPlugWnd, str);
}

void IGraphicsWin::CloseWindow()
{
  if (mPlugWnd)
  {
    if (mTooltipWnd)
    {
      DestroyWindow(mTooltipWnd);
      mTooltipWnd = 0;
      mShowingTooltip = false;
      mTooltipIdx = -1;
    }

    DestroyWindow(mPlugWnd);
    mPlugWnd = 0;

    if (--nWndClassReg == 0)
    {
      UnregisterClass(wndClassName, mHInstance);
    }
  }
}

IPopupMenu* IGraphicsWin::GetItemMenu(long idx, long &idxInMenu, long &offsetIdx, IPopupMenu& baseMenu)
{
  long oldIDx = offsetIdx;
  offsetIdx += baseMenu.GetNItems();

  if (idx < offsetIdx)
  {
    idxInMenu = idx - oldIDx;
    return &baseMenu;
  }

  IPopupMenu* menu = 0;

  for(int i = 0; i< baseMenu.GetNItems(); i++)
  {
    IPopupMenuItem* menuItem = baseMenu.GetItem(i);
    if(menuItem->GetSubmenu())
    {
      menu = GetItemMenu(idx, idxInMenu, offsetIdx, *menuItem->GetSubmenu());

      if(menu)
        break;
    }
  }

  return menu;
}

HMENU IGraphicsWin::CreateMenu(IPopupMenu& menu, long* offsetIdx)
{
  HMENU hMenu = CreatePopupMenu();

  int flags = 0;
  long idxSubmenu = 0;
  long offset = *offsetIdx;
  long nItems = menu.GetNItems();
  *offsetIdx += nItems;
  long inc = 0;

  for(int i = 0; i< nItems; i++)
  {
    IPopupMenuItem* menuItem = menu.GetItem(i);

    if (menuItem->GetIsSeparator())
    {
      AppendMenu (hMenu, MF_SEPARATOR, 0, 0);
    }
    else
    {
      const char* str = menuItem->GetText();
      char* titleWithPrefixNumbers = 0;

      if (menu.GetPrefix())
      {
        titleWithPrefixNumbers = (char*)malloc(strlen(str) + 50);

        switch (menu.GetPrefix())
        {
          case 1:
          {
            sprintf(titleWithPrefixNumbers, "%1d: %s", i+1, str); break;
          }
          case 2:
          {
            sprintf(titleWithPrefixNumbers, "%02d: %s", i+1, str); break;
          }
          case 3:
          {
            sprintf(titleWithPrefixNumbers, "%03d: %s", i+1, str); break;
          }
        }
      }

      const char* entryText (titleWithPrefixNumbers ? titleWithPrefixNumbers : str);

      flags = MF_STRING;
      //if (nItems < 160 && pMenu->getNbItemsPerColumn () > 0 && inc && !(inc % _menu->getNbItemsPerColumn ()))
      //  flags |= MF_MENUBARBREAK;

      if (menuItem->GetSubmenu())
      {
        HMENU submenu = CreateMenu(*menuItem->GetSubmenu(), offsetIdx);
        if (submenu)
        {
          AppendMenu(hMenu, flags|MF_POPUP|MF_ENABLED, (UINT_PTR)submenu, (const TCHAR*)entryText);
        }
      }
      else
      {
        if (menuItem->GetEnabled())
          flags |= MF_ENABLED;
        else
          flags |= MF_GRAYED;
        if (menuItem->GetIsTitle())
          flags |= MF_DISABLED;
        if (menuItem->GetChecked())
          flags |= MF_CHECKED;
        else
          flags |= MF_UNCHECKED;

        AppendMenu(hMenu, flags, offset + inc, entryText);
      }

      if(titleWithPrefixNumbers)
        FREE_NULL(titleWithPrefixNumbers);
    }
    inc++;
  }

  return hMenu;
}

IPopupMenu* IGraphicsWin::CreateIPopupMenu(IPopupMenu& menu, IRECT& areaRect)
{
  ReleaseMouseCapture();

  long offsetIdx = 0;
  HMENU hMenu = CreateMenu(menu, &offsetIdx);
  IPopupMenu* result = 0;

  if(hMenu)
  {
    POINT cPos;

    cPos.x = areaRect.L;
    cPos.y = areaRect.B;

    ClientToScreen(mPlugWnd, &cPos);

    if (TrackPopupMenu(hMenu,
                       TPM_LEFTALIGN,
                       cPos.x,
                       cPos.y,
                       0,
                       mPlugWnd,
                       0))
    {
      MSG msg;
      if (PeekMessage(&msg, mPlugWnd, WM_COMMAND, WM_COMMAND, PM_REMOVE))
      {
        if (HIWORD(msg.wParam) == 0)
        {
          long res = LOWORD(msg.wParam);
          if (res != -1)
          {
            long idx = 0;
            offsetIdx = 0;
            IPopupMenu* resultMenu = GetItemMenu(res, idx, offsetIdx, menu);
            if(resultMenu)
            {
              result = resultMenu;
              result->SetChosenItemIdx(idx);
            }
          }
        }
      }
    }
    DestroyMenu(hMenu);

    RECT r = { 0, 0, Width(), Height() };
    InvalidateRect(mPlugWnd, &r, FALSE);
  }
  return result;
}

void IGraphicsWin::CreateTextEntry(IControl* pControl, const IText& text, const IRECT& textRect, const char* str, IParam* pParam)
{
  if (!pControl || mParamEditWnd) return;

  DWORD editStyle;

  switch ( text.mAlign )
  {
    case IText::kAlignNear:   editStyle = ES_LEFT;   break;
    case IText::kAlignFar:    editStyle = ES_RIGHT;  break;
    case IText::kAlignCenter:
    default:                  editStyle = ES_CENTER; break;
  }

  mParamEditWnd = CreateWindow("EDIT", str, ES_AUTOHSCROLL /*only works for left aligned text*/ | WS_CHILD | WS_VISIBLE | ES_MULTILINE | editStyle,
    textRect.L, textRect.T, textRect.W()+1, textRect.H()+1,
    mPlugWnd, (HMENU) PARAM_EDIT_ID, mHInstance, 0);

  HFONT font = CreateFont(text.mSize, 0, 0, 0, text.mStyle == IText::kStyleBold ? FW_BOLD : 0, text.mStyle == IText::kStyleItalic ? TRUE : 0, 0, 0, 0, 0, 0, 0, 0, text.mFont);

  SendMessage(mParamEditWnd, EM_LIMITTEXT, (WPARAM) pControl->GetTextEntryLength(), 0);
  SendMessage(mParamEditWnd, WM_SETFONT, (WPARAM) font, 0);
  SendMessage(mParamEditWnd, EM_SETSEL, 0, -1);

  SetFocus(mParamEditWnd);

  mDefEditProc = (WNDPROC) SetWindowLongPtr(mParamEditWnd, GWLP_WNDPROC, (LONG_PTR) ParamEditProc);
  SetWindowLongPtr(mParamEditWnd, GWLP_USERDATA, 0xdeadf00b);

  //DeleteObject(font);

  mEdControl = pControl;
  mEdParam = pParam; // could be 0
}

void GetModulePath(HMODULE hModule, WDL_String& path)
{
  path.Set("");
  char pathCStr[MAX_PATH_LEN];
  pathCStr[0] = '\0';
  if (GetModuleFileName(hModule, pathCStr, MAX_PATH_LEN))
  {
    int s = -1;
    for (int i = 0; i < strlen(pathCStr); ++i)
    {
      if (pathCStr[i] == '\\')
      {
        s = i;
      }
    }
    if (s >= 0 && s + 1 < strlen(pathCStr))
    {
      path.Set(pathCStr, s + 1);
    }
  }
}

void IGraphicsWin::HostPath(WDL_String& path)
{
  GetModulePath(0, path);
}

void IGraphicsWin::PluginPath(WDL_String& path)
{
  GetModulePath(mHInstance, path);
}

void IGraphicsWin::DesktopPath(WDL_String& path)
{
  #ifndef __MINGW_H // TODO: alternative for gcc?
  TCHAR strPath[MAX_PATH_LEN];
  SHGetSpecialFolderPath( 0, strPath, CSIDL_DESKTOP, FALSE );
  path.Set(strPath, MAX_PATH_LEN);
  #endif
}

void IGraphicsWin::AppSupportPath(WDL_String& path, bool isSystem)
{
#ifndef __MINGW_H // TODO: alternative for gcc?
  TCHAR strPath[MAX_PATH_LEN];

  if (isSystem)
    SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, strPath);
  else
    SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, strPath);

  path.Set(strPath, MAX_PATH_LEN);
#endif
}

void IGraphicsWin::VST3PresetsPath(WDL_String& path, bool isSystem)
{
  TCHAR strPath[MAX_PATH_LEN];

  if (!isSystem)
  {
    TCHAR strPath[MAX_PATH_LEN];
    SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, strPath);
    path.Set(strPath, MAX_PATH_LEN);
  }
  else
  {
    SHGetFolderPathA(NULL, CSIDL_COMMON_APPDATA, NULL, 0, strPath);
    path.Set(strPath, MAX_PATH_LEN);
  }

  path.AppendFormatted(MAX_PATH_LEN, "\\VST3 Presets\\%s\\%s", mPlug.GetMfrName(), mPlug.GetProductName());
}

bool IGraphicsWin::RevealPathInExplorerOrFinder(WDL_String& path, bool select)
{
  bool success = false;
  
  if (path.GetLength())
  {
    TCHAR winDir[MAX_PATH];
    UINT len = GetSystemDirectory(winDir, MAX_PATH);
    
    if (len || !(len > MAX_PATH - 2))
    {
      winDir[len]   = L'\\';
      winDir[++len] = L'\0';
      
      WDL_String explorerParams;
      
      if(select)
        explorerParams.Append("/select,");
      
      explorerParams.Append("\"");
      explorerParams.Append(path.Get());
      explorerParams.Append("\\\"");
      
      HINSTANCE result;
      
      if ((result=::ShellExecute(NULL, "open", "explorer.exe", (const TCHAR*)explorerParams.Get(), winDir, SW_SHOWNORMAL)) <= (HINSTANCE) 32)
        success = true;
    }
  }
  
  return success;
}

//TODO: this method needs rewriting
void IGraphicsWin::PromptForFile(WDL_String& filename, WDL_String& path, EFileAction action, const char* extensions)
{
  if (!WindowIsOpen())
  {
    filename.Set("");
    return;
  }

  char fnCStr[MAX_PATH_LEN];
  char dirCStr[MAX_PATH_LEN];

  if (filename.GetLength())
    strcpy(fnCStr, filename.Get());
  else
    fnCStr[0] = '\0';

  dirCStr[0] = '\0';

  if (!path.GetLength())
  {
    DesktopPath(path);
  }

  strcpy(dirCStr, path.Get());

  OPENFILENAME ofn;
  memset(&ofn, 0, sizeof(OPENFILENAME));

  ofn.lStructSize = sizeof(OPENFILENAME);
  ofn.hwndOwner = mPlugWnd;
  ofn.lpstrFile = fnCStr;
  ofn.nMaxFile = MAX_PATH_LEN - 1;
  ofn.lpstrInitialDir = dirCStr;
  ofn.Flags = OFN_PATHMUSTEXIST;

  if (CSTR_NOT_EMPTY(extensions))
  {
    char extStr[256];
    char defExtStr[16];
    int i, p, n = strlen(extensions);
    bool seperator = true;

    for (i = 0, p = 0; i < n; ++i)
    {
      if (seperator)
      {
        if (p)
        {
          extStr[p++] = ';';
        }
        seperator = false;
        extStr[p++] = '*';
        extStr[p++] = '.';
      }

      if (extensions[i] == ' ')
      {
        seperator = true;
      }
      else
      {
        extStr[p++] = extensions[i];
      }
    }
    extStr[p++] = '\0';

    strcpy(&extStr[p], extStr);
    extStr[p + p] = '\0';
    ofn.lpstrFilter = extStr;

    for (i = 0, p = 0; i < n && extensions[i] != ' '; ++i)
    {
      defExtStr[p++] = extensions[i];
    }

    defExtStr[p++] = '\0';
    ofn.lpstrDefExt = defExtStr;
  }

  bool rc = false;
  switch (action)
  {
    case kFileSave:
      ofn.Flags |= OFN_OVERWRITEPROMPT;
      rc = GetSaveFileName(&ofn);
      break;

    case kFileOpen:
    default:
      ofn.Flags |= OFN_FILEMUSTEXIST;
      rc = GetOpenFileName(&ofn);
      break;
  }

  if (rc)
  {
    char drive[_MAX_DRIVE];
    #ifndef __MINGW_H // TODO: alternative for gcc
    if(_splitpath_s(ofn.lpstrFile, drive, sizeof(drive), dirCStr, sizeof(dirCStr), NULL, 0, NULL, 0) == 0)
    {
      path.SetFormatted(MAX_PATH_LEN, "%s%s", drive, dirCStr);
    }
    #endif
    filename.Set(ofn.lpstrFile);
  }
  else
  {
    filename.Set("");
  }
}

UINT_PTR CALLBACK CCHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
  if (uiMsg == WM_INITDIALOG && lParam)
  {
    CHOOSECOLOR* cc = (CHOOSECOLOR*) lParam;
    if (cc && cc->lCustData)
    {
      char* str = (char*) cc->lCustData;
      SetWindowText(hdlg, str);
    }
  }
  return 0;
}

bool IGraphicsWin::PromptForColor(IColor& color, const char* prompt)
{
  if (!mPlugWnd)
  {
    return false;
  }
  if (!mCustomColorStorage)
  {
    mCustomColorStorage = (COLORREF*) calloc(16, sizeof(COLORREF));
  }
  CHOOSECOLOR cc;
  memset(&cc, 0, sizeof(CHOOSECOLOR));
  cc.lStructSize = sizeof(CHOOSECOLOR);
  cc.hwndOwner = mPlugWnd;
  cc.rgbResult = RGB(color.R, color.G, color.B);
  cc.lpCustColors = mCustomColorStorage;
  cc.lCustData = (LPARAM) prompt;
  cc.lpfnHook = CCHookProc;
  cc.Flags = CC_RGBINIT | CC_ANYCOLOR | CC_FULLOPEN | CC_SOLIDCOLOR | CC_ENABLEHOOK;

  if (ChooseColor(&cc))
  {
    color.R = GetRValue(cc.rgbResult);
    color.G = GetGValue(cc.rgbResult);
    color.B = GetBValue(cc.rgbResult);
    return true;
  }
  return false;
}

bool IGraphicsWin::OpenURL(const char* url, const char* msgWindowTitle, const char* confirmMsg, const char* errMsgOnFailure)
{
  if (confirmMsg && MessageBox(mPlugWnd, confirmMsg, msgWindowTitle, MB_YESNO) != IDYES)
  {
    return false;
  }
  DWORD inetStatus = 0;
  if (InternetGetConnectedState(&inetStatus, 0))
  {
    if ((int) ShellExecute(mPlugWnd, "open", url, 0, 0, SW_SHOWNORMAL) > MAX_INET_ERR_CODE)
    {
      return true;
    }
  }
  if (errMsgOnFailure)
  {
    MessageBox(mPlugWnd, errMsgOnFailure, msgWindowTitle, MB_OK);
  }
  return false;
}

void IGraphicsWin::SetTooltip(const char* tooltip)
{
  TOOLINFO ti = { TTTOOLINFOA_V2_SIZE, 0, mPlugWnd, (UINT_PTR)mPlugWnd };
  ti.lpszText = (LPTSTR)tooltip;
  SendMessage(mTooltipWnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
}

void IGraphicsWin::ShowTooltip()
{
  const char* tooltip = GetControl(mTooltipIdx)->GetTooltip();
  if (tooltip)
  {
    assert(strlen(tooltip) < 80);
    SetTooltip(tooltip);
    mShowingTooltip = true;
  }
}

void IGraphicsWin::HideTooltip()
{
  if (mShowingTooltip)
  {
    SetTooltip(NULL);
    mShowingTooltip = false;
  }
}

bool IGraphicsWin::GetTextFromClipboard(WDL_String& str)
{
  bool success = false;
  HGLOBAL hglb;
  
  if (IsClipboardFormatAvailable(CF_UNICODETEXT))
  {
    if(OpenClipboard(0))
    {
      hglb = GetClipboardData(CF_UNICODETEXT);
      
      if(hglb != NULL)
      {
        WCHAR *orig_str = (WCHAR*)GlobalLock(hglb);
        
        if (orig_str != NULL)
        {
          int orig_len = (int) wcslen(orig_str);
          
          orig_len += 1;
          
          // find out how much space is needed
          int new_len = WideCharToMultiByte(CP_UTF8,
                                            0,
                                            orig_str,
                                            orig_len,
                                            0,
                                            0,
                                            NULL,
                                            NULL);
          
          if (new_len > 0)
          {
            char *new_str = new char[new_len + 1];
            
            int num_chars = WideCharToMultiByte(CP_UTF8,
                                                0,
                                                orig_str,
                                                orig_len,
                                                new_str,
                                                new_len,
                                                NULL,
                                                NULL);
            
            if (num_chars > 0)
            {
              success = true;
              str.Set(new_str);
            }
            
            delete [] new_str;
          }
          
          GlobalUnlock(hglb);
        }
      }
    }
    
    CloseClipboard();
  }
  
  if(!success)
    str.Set("");
  
  return success;
}

BOOL IGraphicsWin::EnumResNameProc(HANDLE module, LPCTSTR type, LPTSTR name, LONG param)
{
  if (IS_INTRESOURCE(name)) return true; // integer resources not wanted
  else {
    WDL_String* search = (WDL_String*) param;
    if (search != 0 && name != 0)
    {
      //strip off extra quotes
      WDL_String strippedName(strlwr(name+1)); 
      strippedName.SetLen(strippedName.GetLength() - 1);

      if (strcmp(search->Get(), strippedName.Get()) == 0) // if we are looking for a resource with this name
      {
        search->SetFormatted(strippedName.GetLength() + 7, "found: %s", strippedName.Get());
        return false;
      }
    }
  }

  return true; // keep enumerating
}

bool IGraphicsWin::OSFindResource(const char* name, const char* type, WDL_String& result)
{
  WDL_String typeUppercase(type);
  WDL_String search(name);
  EnumResourceNames(mHInstance, _strupr(typeUppercase.Get()), (ENUMRESNAMEPROC)EnumResNameProc, (LONG_PTR) &search);
  
  if (strstr(search.Get(), "found: ") != 0)
  {
    result.SetFormatted(MAX_PATH, "\"%s\"", search.Get() + 7, search.GetLength() - 7); // 7 = strlen("found: ")
    return true;
  }
  else
  {
    //TODO: search some other path - for instance if the plug-in developer wishes to store graphics resources in Program Files, to reduce the size of plug-in binaries
    return false;
  }

  return false;
}
