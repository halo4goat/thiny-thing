#include <d3d11.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

#include "Bot.h"
#include "Config.h"
#include "Process.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;

void createRtv() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}
void releaseRtv() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx)))
        return false;
    createRtv();
    return true;
}
void destroyDevice() {
    releaseRtv();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return true;
    switch (msg) {
        case WM_SIZE:
            if (w == SIZE_MINIMIZED) return 0;
            g_resizeW = LOWORD(l); g_resizeH = HIWORD(l);
            return 0;
        case WM_SYSCOMMAND:
            if ((w & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

struct App {
    GameOffsets offsets;
    GameProcess proc;
    Bot bot{proc};
    std::filesystem::path exeDir, cfgPath;
    std::string cfgError, attachError, actionError, fileMsg, traceMsg;
    char macroPath[260] = "thinkingspace2.gdm";
    int inputKind = 0;

    std::filesystem::path macroFile() const {
        std::filesystem::path p = std::filesystem::path(macroPath);
        return p.is_absolute() ? p : exeDir / p;
    }

    void loadMacroFile() {
        Macro m; std::string err;
        if (loadMacro(macroFile(), m, err)) {
            fileMsg = "loaded " + std::to_string(m.actions.size()) + " actions";
            bot.setMacro(std::move(m));
        } else {
            fileMsg = err;
            return;
        }
        // The per-frame reference track sits next to the macro with the extension .ref
        RefTrack r; std::string rerr;
        std::filesystem::path rp = macroFile();
        rp.replace_extension(".ref");
        if (loadRef(rp, r, rerr)) {
            fileMsg += "; reference track: " + std::to_string(r.x.size()) + " frames";
            bot.setRef(std::move(r));
        } else {
            fileMsg += "; no reference track (" + rerr + ")";
            bot.setRef(RefTrack{});
        }
    }

    void saveTrace() {
        const auto rows = bot.traceSnapshot();
        std::ofstream f(exeDir / "trace.csv", std::ios::trunc);
        if (!f) { traceMsg = "cannot write trace.csv next to the exe"; return; }
        f << "t_ms,x,y,idx,event\n" << std::fixed << std::setprecision(4);
        for (const auto& r : rows) f << r.tMs << ',' << r.x << ',' << r.y << ',' << r.idx << ',' << r.event << '\n';
        traceMsg = "saved " + std::to_string(rows.size()) + " rows to trace.csv";
    }

    void reloadConfig() {
        bot.stop();
        proc.detach();
        cfgError.clear();
        GameOffsets o;
        if (loadOffsets(cfgPath, o, cfgError)) offsets = std::move(o);
    }
};

void toggle(App& a, Bot::Mode m) {
    a.actionError.clear();
    if (a.bot.mode() == m) { a.bot.stop(); return; }
    a.bot.start(m, static_cast<InputKind>(a.inputKind), a.actionError);
}

void drawUi(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("GD Macro Menu", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    const bool running = a.bot.mode() != Bot::Mode::Idle;

    ImGui::SeparatorText("Game");
    ImGui::Text("Attached: %s", a.proc.attached() ? "yes" : "no");
    if (a.proc.attached()) ImGui::Text("Observed module size: 0x%llX", (unsigned long long)a.proc.observedModuleSize());
    if (!a.offsets.complete()) ImGui::TextWrapped("Offsets incomplete, missing: %s", a.offsets.missing().c_str());
    if (!a.cfgError.empty()) ImGui::TextWrapped("Config: %s", a.cfgError.c_str());
    if (!a.attachError.empty()) ImGui::TextWrapped("Attach: %s", a.attachError.c_str());
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Attach")) { a.attachError.clear(); a.proc.attach(a.offsets, a.attachError); }
    ImGui::SameLine();
    if (ImGui::Button("Detach")) a.proc.detach();
    ImGui::SameLine();
    if (ImGui::Button("Reload offsets")) a.reloadConfig();
    ImGui::EndDisabled();

    float x = 0, y = 0;
    ImGui::SeparatorText("Live");
    if (a.proc.attached()) {
        ReadResult r = a.proc.readPlayerPos(x, y);
        if (r == ReadResult::Ok) ImGui::Text("Player X: %.3f   Y: %.3f", x, y);
        else ImGui::Text("Player X: %s", describe(r));
    }

    ImGui::SeparatorText("Input / tuning (uncalibrated)");
    ImGui::BeginDisabled(running);
    ImGui::Combo("Click input", &a.inputKind, "Space\0Up arrow\0Left mouse\0");
    ImGui::EndDisabled();
    float lead = a.bot.leadMs.load();
    if (ImGui::InputFloat("Lead (ms, negative = click later)", &lead, 0.5f, 2.f, "%.1f")) a.bot.leadMs = std::clamp(lead, -50.f, 100.f);
    float thr = a.bot.resetThreshold.load();
    if (ImGui::InputFloat("Restart threshold (X units)", &thr, 1.f, 10.f, "%.1f")) a.bot.resetThreshold = std::clamp(thr, 1.f, 100000.f);

    bool iso = a.bot.isolatedMouse.load();
    ImGui::BeginDisabled(running || a.inputKind == 2);
    if (ImGui::Checkbox("Isolated mouse click on every macro press", &iso)) a.bot.isolatedMouse = iso;
    float holdMs = a.bot.isolatedHoldMs.load();
    if (ImGui::InputFloat("Isolated click hold (ms)", &holdMs, 1.f, 5.f, "%.1f"))
        a.bot.isolatedHoldMs = std::clamp(holdMs, 0.f, 100.f);
    ImGui::EndDisabled();
    if (a.inputKind == 2) ImGui::TextDisabled("Isolated click is ignored when the main input is already the mouse.");
    ImGui::TextWrapped("Note: the game may treat the mouse button as the same jump button, so the mouse release can cut holds short. Leave this off unless you are testing it.");

    bool snd = a.bot.clickSound.load();
    ImGui::BeginDisabled(running);
    if (ImGui::Checkbox("Menu click sound on each macro press/release", &snd)) a.bot.clickSound = snd;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Test sound")) a.bot.testClickSound();
    ImGui::TextDisabled("The sound is played by this menu. The game itself makes no sound when you jump.");

    bool yfix = a.bot.yFix.load();
    ImGui::BeginDisabled(running || !a.proc.attached() || !a.proc.canWrite() || a.bot.refSize() == 0);
    if (ImGui::Checkbox("EXPERIMENTAL: correct Y to the replay every step (writes game memory)", &yfix)) a.bot.yFix = yfix;
    ImGui::EndDisabled();
    ImGui::TextDisabled("Needs the .ref file next to the macro and write access. Reference frames loaded: %zu. Write access: %s.",
                        a.bot.refSize(), a.proc.attached() ? (a.proc.canWrite() ? "yes" : "no") : "attach first");

    ImGui::SeparatorText("Macro (relative paths are next to the exe)");
    ImGui::InputText("File", a.macroPath, sizeof(a.macroPath));
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Load")) a.loadMacroFile();
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string err;
        a.fileMsg = saveMacro(a.macroFile(), a.bot.macro(), err) ? "saved" : err;
    }
    ImGui::EndDisabled();
    ImGui::Text("Actions in memory: %zu", a.bot.macro().actions.size());
    if (!a.fileMsg.empty()) ImGui::TextWrapped("%s", a.fileMsg.c_str());

    ImGui::SeparatorText("Run  (hotkeys: F6 record, F7 play)");
    if (ImGui::Button(a.bot.mode() == Bot::Mode::Recording ? "Stop recording" : "Record")) toggle(a, Bot::Mode::Recording);
    ImGui::SameLine();
    if (ImGui::Button(a.bot.mode() == Bot::Mode::Playing ? "Stop playback" : "Play")) toggle(a, Bot::Mode::Playing);
    ImGui::Text("Status: %s", a.bot.status().c_str());
    ImGui::Text("Clicks sent: %llu   Recorded: %llu   Attempts: %llu",
                (unsigned long long)a.bot.clicksSent.load(),
                (unsigned long long)a.bot.clicksRecorded.load(),
                (unsigned long long)a.bot.attempts.load());
    ImGui::Text("Y corrections written: %llu   failed: %llu",
                (unsigned long long)a.bot.yFixWritten.load(), (unsigned long long)a.bot.yFixFailed.load());
    ImGui::Text("Isolated mouse clicks: %llu sent, %llu skipped",
                (unsigned long long)a.bot.isolatedSent.load(),
                (unsigned long long)a.bot.isolatedSkipped.load());
    const int fired = a.bot.lastFiredIdx.load();
    if (fired < 0) {
        ImGui::Text("Last fired action: none yet");
    } else {
        const float mx = a.bot.lastFiredMacroX.load(), ax = a.bot.lastFiredActualX.load();
        ImGui::Text("Last fired action #%d: macro X %.2f, fired when X read %.2f (diff %+.2f)", fired, mx, ax, ax - mx);
    }
    ImGui::Text("Longest gap between X reads: %u us", a.bot.maxLoopGapUs.load());
    ImGui::Text("Measured speed: %.1f units/s   Skipped as stale: %llu", a.bot.estSpeed.load(),
                (unsigned long long)a.bot.staleSkipped.load());
    if (ImGui::Button("Save trace to trace.csv")) a.saveTrace();
    if (!a.traceMsg.empty()) ImGui::TextWrapped("%s", a.traceMsg.c_str());
    ImGui::SeparatorText("Deaths this run (newest last)");
    const auto deaths = a.bot.deaths();
    for (std::size_t i = deaths.size() > 8 ? deaths.size() - 8 : 0; i < deaths.size(); ++i) {
        const auto& d = deaths[i];
        ImGui::TextWrapped("X %.1f | last fired #%d (macro %.1f, read %.1f) | next #%d at %.1f",
                           d.x, d.lastIdx, d.lastMacroX, d.lastActualX, d.nextIdx, d.nextMacroX);
    }
    if (!a.actionError.empty()) ImGui::TextWrapped("%s", a.actionError.c_str());

    ImGui::End();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wndProc, 0, 0, inst, nullptr, nullptr, nullptr, nullptr, L"GdMacroMenu", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"GD Macro Menu", WS_OVERLAPPEDWINDOW, 100, 100, 560, 900,
                              nullptr, nullptr, inst, nullptr);
    if (!createDevice(hwnd)) { destroyDevice(); UnregisterClassW(wc.lpszClassName, inst); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    {
        App app;
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        app.exeDir = std::filesystem::path(exe).parent_path();
        app.cfgPath = app.exeDir / "gd_offsets.cfg";
        app.reloadConfig();
        if (std::filesystem::exists(app.macroFile())) app.loadMacroFile();

        bool prevF6 = false, prevF7 = false;
        bool done = false;
        while (!done) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT) done = true;
            }
            if (done) break;

            if (g_resizeW && g_resizeH) {
                releaseRtv();
                g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
                g_resizeW = g_resizeH = 0;
                createRtv();
            }

            const bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            const bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            if (f6 && !prevF6) toggle(app, Bot::Mode::Recording);
            if (f7 && !prevF7) toggle(app, Bot::Mode::Playing);
            prevF6 = f6; prevF7 = f7;

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            drawUi(app);
            ImGui::Render();

            const float clear[4] = {0.08f, 0.08f, 0.10f, 1.f};
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            g_ctx->ClearRenderTargetView(g_rtv, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_swap->Present(1, 0);
        }
        app.bot.stop();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}
