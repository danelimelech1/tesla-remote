// ============================================================
//  Tesla Remote – CLIENT  (runs on the remote machine)
//  Captures screen, streams JPEG frames over TCP to the Server.
// ============================================================
#include <windows.h>

// Boost.Asio
#include <boost/asio.hpp>

// GLFW + OpenGL
#include <GLFW/glfw3.h>

// Dear ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// stb – JPEG encoder
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "../common/protocol.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using tcp     = asio::ip::tcp;

// ─── App state ────────────────────────────────────────────────────────────────
struct AppState {
    std::atomic<bool>   connected{false};
    std::atomic<bool>   capturing{false};
    std::atomic<bool>   shouldStop{false};
    std::atomic<int>    targetFps{20};
    std::atomic<int>    jpegQuality{75};
    std::atomic<float>  currentFps{0.0f};
    std::atomic<size_t> bytesSent{0};

    std::string  serverIp{"127.0.0.1"};
    uint16_t     serverPort{tvproto::DEFAULT_PORT};

    std::mutex   statusMutex;
    std::string  statusMsg{"Ready to connect"};
};

static AppState         g_app;
static asio::io_context g_ioc;
static std::unique_ptr<tcp::socket> g_socket;

// ─── JPEG callback ────────────────────────────────────────────────────────────
static void jpegCb(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    auto* p = static_cast<uint8_t*>(data);
    v->insert(v->end(), p, p + size);
}

// ─── Capture thread ───────────────────────────────────────────────────────────
static void captureThread() {
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    HDC    scrDC = GetDC(nullptr);
    HDC    memDC = CreateCompatibleDC(scrDC);
    HBITMAP bmp  = CreateCompatibleBitmap(scrDC, sw, sh);
    HGDIOBJ old  = SelectObject(memDC, bmp);

    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = sw;
    bi.biHeight      = -sh;
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    std::vector<uint8_t> bgra(sw * sh * 4);
    std::vector<uint8_t> rgb (sw * sh * 3);
    std::vector<uint8_t> jpeg;
    uint32_t frameNum = 0;
    size_t   bwTick   = 0;

    using Clock = std::chrono::steady_clock;
    auto bwStart = Clock::now();

    while (!g_app.shouldStop && g_app.capturing) {
        auto t0 = Clock::now();

        BitBlt(memDC, 0, 0, sw, sh, scrDC, 0, 0, SRCCOPY | CAPTUREBLT);
        GetDIBits(scrDC, bmp, 0, sh, bgra.data(),
                  reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

        for (int i = 0; i < sw * sh; ++i) {
            rgb[i*3+0] = bgra[i*4+2];
            rgb[i*3+1] = bgra[i*4+1];
            rgb[i*3+2] = bgra[i*4+0];
        }

        jpeg.clear();
        stbi_write_jpg_to_func(jpegCb, &jpeg, sw, sh, 3,
                               rgb.data(), g_app.jpegQuality);

        tvproto::PacketHeader hdr{};
        hdr.magic        = tvproto::MAGIC;
        hdr.type         = static_cast<uint8_t>(tvproto::PacketType::FrameData);
        hdr.payload_size = static_cast<uint32_t>(jpeg.size());
        hdr.frame_width  = static_cast<uint32_t>(sw);
        hdr.frame_height = static_cast<uint32_t>(sh);
        hdr.frame_number = frameNum++;

        try {
            asio::write(*g_socket, asio::buffer(&hdr, sizeof(hdr)));
            asio::write(*g_socket, asio::buffer(jpeg));
            bwTick += sizeof(hdr) + jpeg.size();
        } catch (...) {
            std::lock_guard<std::mutex> lk(g_app.statusMutex);
            g_app.statusMsg = "Connection lost";
            break;
        }

        float bwElapsed = std::chrono::duration<float>(Clock::now() - bwStart).count();
        if (bwElapsed >= 1.0f) {
            g_app.bytesSent = bwTick;
            bwTick   = 0;
            bwStart  = Clock::now();
        }

        auto target = std::chrono::microseconds(1'000'000 / g_app.targetFps);
        auto spent  = Clock::now() - t0;
        if (spent < target) std::this_thread::sleep_for(target - spent);

        float total = std::chrono::duration<float>(Clock::now() - t0).count();
        if (total > 0.0f) g_app.currentFps = 1.0f / total;
    }

    SelectObject(memDC, old);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, scrDC);
    g_app.capturing = false;
    g_app.connected = false;
}

// ─── Tesla theme ─────────────────────────────────────────────────────────────
static void applyTeslaTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 14.0f;
    s.ChildRounding     = 10.0f;
    s.FrameRounding     = 8.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.PopupRounding     = 12.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = {12.0f,  8.0f};
    s.ItemInnerSpacing  = { 8.0f,  6.0f};
    s.WindowPadding     = {22.0f, 20.0f};
    s.FramePadding      = {12.0f,  7.0f};
    s.GrabMinSize       = 8.0f;
    s.ScrollbarSize     = 10.0f;

    const ImVec4 red  = {0.908f,0.129f,0.153f,1.0f};
    const ImVec4 redH = {1.000f,0.200f,0.220f,1.0f};
    const ImVec4 redA = {0.700f,0.080f,0.100f,1.0f};
    const ImVec4 bg0  = {0.051f,0.051f,0.051f,1.0f};
    const ImVec4 bg1  = {0.078f,0.078f,0.078f,1.0f};
    const ImVec4 bg2  = {0.118f,0.118f,0.118f,1.0f};
    const ImVec4 bg3  = {0.165f,0.165f,0.165f,1.0f};
    const ImVec4 bord = {0.180f,0.180f,0.180f,1.0f};

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = bg0;
    c[ImGuiCol_ChildBg]              = bg1;
    c[ImGuiCol_PopupBg]              = bg1;
    c[ImGuiCol_Border]               = bord;
    c[ImGuiCol_BorderShadow]         = {0,0,0,0};
    c[ImGuiCol_FrameBg]              = bg2;
    c[ImGuiCol_FrameBgHovered]       = bg3;
    c[ImGuiCol_FrameBgActive]        = {0.22f,0.22f,0.22f,1.0f};
    c[ImGuiCol_TitleBg]              = bg0;
    c[ImGuiCol_TitleBgActive]        = bg0;
    c[ImGuiCol_TitleBgCollapsed]     = bg0;
    c[ImGuiCol_MenuBarBg]            = bg1;
    c[ImGuiCol_ScrollbarBg]          = bg0;
    c[ImGuiCol_ScrollbarGrab]        = bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = {0.25f,0.25f,0.25f,1.0f};
    c[ImGuiCol_ScrollbarGrabActive]  = red;
    c[ImGuiCol_CheckMark]            = red;
    c[ImGuiCol_SliderGrab]           = red;
    c[ImGuiCol_SliderGrabActive]     = redH;
    c[ImGuiCol_Button]               = red;
    c[ImGuiCol_ButtonHovered]        = redH;
    c[ImGuiCol_ButtonActive]         = redA;
    c[ImGuiCol_Header]               = {0.908f,0.129f,0.153f,0.35f};
    c[ImGuiCol_HeaderHovered]        = {0.908f,0.129f,0.153f,0.60f};
    c[ImGuiCol_HeaderActive]         = red;
    c[ImGuiCol_Separator]            = bord;
    c[ImGuiCol_SeparatorHovered]     = red;
    c[ImGuiCol_SeparatorActive]      = redH;
    c[ImGuiCol_ResizeGrip]           = {0.908f,0.129f,0.153f,0.25f};
    c[ImGuiCol_ResizeGripHovered]    = {0.908f,0.129f,0.153f,0.70f};
    c[ImGuiCol_ResizeGripActive]     = red;
    c[ImGuiCol_Tab]                  = bg1;
    c[ImGuiCol_TabHovered]           = redH;
    c[ImGuiCol_TabActive]            = red;
    c[ImGuiCol_TabUnfocused]         = bg1;
    c[ImGuiCol_TabUnfocusedActive]   = {0.20f,0.05f,0.06f,1.0f};
    c[ImGuiCol_Text]                 = {1.0f,1.0f,1.0f,1.0f};
    c[ImGuiCol_TextDisabled]         = {0.45f,0.45f,0.45f,1.0f};
    c[ImGuiCol_PlotLines]            = red;
    c[ImGuiCol_PlotLinesHovered]     = redH;
    c[ImGuiCol_PlotHistogram]        = red;
    c[ImGuiCol_PlotHistogramHovered] = redH;
    c[ImGuiCol_NavHighlight]         = red;
}

// ─── Connect ──────────────────────────────────────────────────────────────────
static bool doConnect(const std::string& ip, uint16_t port) {
    try {
        boost::system::error_code ec;
        g_socket = std::make_unique<tcp::socket>(g_ioc);
        tcp::endpoint ep(asio::ip::make_address(ip, ec), port);
        if (ec) throw std::runtime_error("Bad IP");
        g_socket->connect(ep);
        g_socket->set_option(tcp::no_delay(true));
        return true;
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lk(g_app.statusMutex);
        g_app.statusMsg = std::string("Error: ") + ex.what();
        return false;
    }
}

// ─── UI ───────────────────────────────────────────────────────────────────────
static void renderUI(GLFWwindow* win) {
    const ImGuiIO& io = ImGui::GetIO();
    const float W     = io.DisplaySize.x;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    constexpr ImGuiWindowFlags FLAGS =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##root", nullptr, FLAGS);

    // ── Logo ──
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.908f,0.129f,0.153f,1.0f));
    const char* logo = "TESLA REMOTE";
    ImGui::SetCursorPosX((W - ImGui::CalcTextSize(logo).x) * 0.5f);
    ImGui::Text("%s", logo);
    ImGui::PopStyleColor();
    const char* sub = "Screen Share  |  Client";
    ImGui::SetCursorPosX((W - ImGui::CalcTextSize(sub).x) * 0.5f);
    ImGui::TextDisabled("%s", sub);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float padX  = 22.0f;
    const float inputW = W - padX * 2.0f;

    if (!g_app.connected) {
        static char ipBuf[64]  = "127.0.0.1";
        static char portBuf[8] = "7788";

        ImGui::Text("Server IP");
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText("##ip", ipBuf, sizeof(ipBuf));

        ImGui::Spacing();
        ImGui::Text("Port");
        ImGui::SetNextItemWidth(inputW);
        ImGui::InputText("##port", portBuf, sizeof(portBuf));

        ImGui::Spacing();
        ImGui::Text("JPEG Quality  (%d%%)", g_app.jpegQuality.load());
        ImGui::SetNextItemWidth(inputW);
        int q = g_app.jpegQuality;
        if (ImGui::SliderInt("##q", &q, 20, 100)) g_app.jpegQuality = q;

        ImGui::Spacing();
        ImGui::Text("Target FPS  (%d)", g_app.targetFps.load());
        ImGui::SetNextItemWidth(inputW);
        int f = g_app.targetFps;
        if (ImGui::SliderInt("##f", &f, 5, 60)) g_app.targetFps = f;

        ImGui::Spacing(); ImGui::Spacing();

        if (ImGui::Button("  CONNECT  ", {inputW, 42.0f})) {
            uint16_t p = static_cast<uint16_t>(std::atoi(portBuf));
            g_app.serverIp   = ipBuf;
            g_app.serverPort = p;
            if (doConnect(g_app.serverIp, g_app.serverPort)) {
                g_app.connected = true;
                g_app.capturing = true;
                {
                    std::lock_guard<std::mutex> lk(g_app.statusMutex);
                    g_app.statusMsg = "Streaming...";
                }
                std::thread(captureThread).detach();
            }
        }

        ImGui::Spacing();
        std::string msg;
        {
            std::lock_guard<std::mutex> lk(g_app.statusMutex);
            msg = g_app.statusMsg;
        }
        ImGui::SetCursorPosX((W - ImGui::CalcTextSize(msg.c_str()).x) * 0.5f);
        ImGui::TextDisabled("%s", msg.c_str());

    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f,0.83f,0.67f,1.0f));
        const char* conn = "  CONNECTED";
        ImGui::SetCursorPosX((W - ImGui::CalcTextSize(conn).x) * 0.5f);
        ImGui::Text("%s", conn);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f,0.09f,0.09f,1.0f));
        ImGui::BeginChild("stats", {inputW, 90.0f}, true);
        ImGui::Text("Server     %s : %d", g_app.serverIp.c_str(), (int)g_app.serverPort);
        ImGui::Text("FPS        %.1f", g_app.currentFps.load());
        ImGui::Text("Bandwidth  %.1f KB/s", g_app.bytesSent.load() / 1024.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Text("Quality  (%d%%)", g_app.jpegQuality.load());
        ImGui::SetNextItemWidth(inputW);
        int q = g_app.jpegQuality;
        if (ImGui::SliderInt("##q2", &q, 20, 100)) g_app.jpegQuality = q;

        ImGui::Spacing();
        ImGui::Text("FPS limit  (%d)", g_app.targetFps.load());
        ImGui::SetNextItemWidth(inputW);
        int f = g_app.targetFps;
        if (ImGui::SliderInt("##f2", &f, 5, 60)) g_app.targetFps = f;

        ImGui::Spacing(); ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        {0.18f,0.18f,0.18f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f,0.28f,0.28f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f,0.10f,0.10f,1.0f});
        if (ImGui::Button("  DISCONNECT  ", {inputW, 42.0f})) {
            g_app.shouldStop = true;
            g_app.capturing  = false;
            if (g_socket && g_socket->is_open()) g_socket->close();
            g_app.connected  = false;
            g_app.shouldStop = false;
            std::lock_guard<std::mutex> lk(g_app.statusMutex);
            g_app.statusMsg  = "Disconnected";
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::End();
    (void)win;
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    glfwSetErrorCallback([](int, const char* desc) {
        (void)desc; // errors visible in debugger
    });
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* win = glfwCreateWindow(430, 560, "Tesla Remote – Client", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
    if (!font) io.Fonts->AddFontDefault();

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    applyTeslaTheme();

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUI(win);

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.051f, 0.051f, 0.051f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    g_app.shouldStop = true;
    if (g_socket && g_socket->is_open()) g_socket->close();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
