// ============================================================
//  Tesla Remote – SERVER / VIEWER
//  Accepts TCP connections, receives JPEG frames, displays
//  the remote screen with a full Tesla-themed UI.
// ============================================================
#include <windows.h>

// Boost.Asio
#include <boost/asio.hpp>

// GLFW + OpenGL
#include <GLFW/glfw3.h>

// OpenGL constants not in GL 1.1 Windows headers
#ifndef GL_CLAMP_TO_EDGE
#  define GL_CLAMP_TO_EDGE 0x812F
#endif

static constexpr float MY_PI = 3.14159265358979323846f;

// Dear ImGui
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// stb – JPEG decoder
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "../common/protocol.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

// ─── Shared state ─────────────────────────────────────────────────────────────
struct ViewerState {
    std::atomic<bool>   clientConnected{false};
    std::atomic<bool>   shouldStop{false};
    std::atomic<float>  fps{0.0f};
    std::atomic<float>  bandwidthKBs{0.0f};
    std::atomic<int>    totalFrames{0};
    std::string         clientAddr;

    std::mutex           frameMutex;
    std::vector<uint8_t> frameData;    // RGB pixels
    int                  frameW{0};
    int                  frameH{0};
    std::atomic<bool>    newFrame{false};
};

static ViewerState g_view;

// ─── Network thread ───────────────────────────────────────────────────────────
static void networkThread() {
    try {
        asio::io_context ioc;
        tcp::acceptor acceptor(ioc,
            tcp::endpoint(tcp::v4(), tvproto::DEFAULT_PORT));
        acceptor.set_option(asio::socket_base::reuse_address(true));

        while (!g_view.shouldStop) {
            tcp::socket sock(ioc);
            boost::system::error_code ec;
            acceptor.accept(sock, ec);
            if (ec || g_view.shouldStop) break;

            sock.set_option(tcp::no_delay(true));
            g_view.clientAddr      = sock.remote_endpoint(ec).address().to_string();
            g_view.clientConnected = true;

            using Clock = std::chrono::steady_clock;
            auto   tickStart  = Clock::now();
            size_t tickBytes  = 0;
            int    tickFrames = 0;

            while (!g_view.shouldStop) {
                tvproto::PacketHeader hdr{};
                boost::system::error_code re;
                asio::read(sock, asio::buffer(&hdr, sizeof(hdr)), re);
                if (re) break;
                if (hdr.magic != tvproto::MAGIC) break;

                std::vector<uint8_t> payload(hdr.payload_size);
                asio::read(sock, asio::buffer(payload), re);
                if (re) break;

                tickBytes  += sizeof(hdr) + payload.size();
                tickFrames += 1;

                if (hdr.type == static_cast<uint8_t>(tvproto::PacketType::FrameData)) {
                    int w, h, ch;
                    uint8_t* img = stbi_load_from_memory(
                        payload.data(), static_cast<int>(payload.size()),
                        &w, &h, &ch, 3);
                    if (img) {
                        {
                            std::lock_guard<std::mutex> lk(g_view.frameMutex);
                            g_view.frameData.assign(img, img + w * h * 3);
                            g_view.frameW = w;
                            g_view.frameH = h;
                        }
                        g_view.newFrame = true;
                        stbi_image_free(img);
                        g_view.totalFrames.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                float elapsed = std::chrono::duration<float>(
                    Clock::now() - tickStart).count();
                if (elapsed >= 1.0f) {
                    g_view.fps          = static_cast<float>(tickFrames) / elapsed;
                    g_view.bandwidthKBs = static_cast<float>(tickBytes) / elapsed / 1024.0f;
                    tickFrames = 0;
                    tickBytes  = 0;
                    tickStart  = Clock::now();
                }
            }

            g_view.clientConnected = false;
            g_view.newFrame        = false;
        }
    } catch (...) {}
}

// ─── Tesla theme ─────────────────────────────────────────────────────────────
static void applyTeslaTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 10.0f;
    s.FrameRounding     = 8.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.PopupRounding     = 12.0f;
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = {10.0f,  8.0f};
    s.ItemInnerSpacing  = { 8.0f,  6.0f};
    s.WindowPadding     = {16.0f, 14.0f};
    s.FramePadding      = {10.0f,  6.0f};
    s.GrabMinSize       = 8.0f;
    s.ScrollbarSize     = 10.0f;

    const ImVec4 red  = {0.908f,0.129f,0.153f,1.0f};
    const ImVec4 redH = {1.000f,0.220f,0.240f,1.0f};
    const ImVec4 redA = {0.700f,0.080f,0.100f,1.0f};
    const ImVec4 bg0  = {0.043f,0.043f,0.043f,1.0f};
    const ImVec4 bg1  = {0.070f,0.070f,0.070f,1.0f};
    const ImVec4 bg2  = {0.110f,0.110f,0.110f,1.0f};
    const ImVec4 bg3  = {0.160f,0.160f,0.160f,1.0f};
    const ImVec4 bord = {0.175f,0.175f,0.175f,1.0f};

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

// ─── Spinner animation ────────────────────────────────────────────────────────
static void drawSpinner(ImVec2 center, float radius, float t) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int N = 10;
    for (int i = 0; i < N; ++i) {
        float angle = static_cast<float>(i) / N * 2.0f * MY_PI + t * 2.5f;
        float alpha = static_cast<float>(i + 1) / N;
        ImVec2 p = {center.x + cosf(angle) * radius,
                    center.y + sinf(angle) * radius};
        dl->AddCircleFilled(p, 5.5f,
            IM_COL32(231, 33, 39, static_cast<int>(alpha * 220)));
    }
}

// ─── FPS history ─────────────────────────────────────────────────────────────
static float s_fpsHist[90] = {};
static int   s_fpsIdx = 0;

// ─── Render ───────────────────────────────────────────────────────────────────
static void renderUI(GLuint texId, int texW, int texH) {
    const ImGuiIO& io = ImGui::GetIO();
    const float W     = io.DisplaySize.x;
    const float H     = io.DisplaySize.y;
    const float t     = static_cast<float>(glfwGetTime());

    if (g_view.clientConnected) {
        s_fpsHist[s_fpsIdx % 90] = g_view.fps;
        ++s_fpsIdx;
    }

    constexpr ImGuiWindowFlags ROOT =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar|
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

    // ── Top bar ──────────────────────────────────────────────────────────────
    const float BAR_H = 48.0f;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, BAR_H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f,0.055f,0.055f,1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16.0f, 12.0f});
    ImGui::Begin("##topbar", nullptr, ROOT | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.908f,0.129f,0.153f,1.0f));
    ImGui::Text("TESLA REMOTE");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("  |  Viewer");

    ImGui::SameLine(W - 370.0f);
    if (g_view.clientConnected) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f,0.83f,0.67f,1.0f));
        ImGui::Text("●  %s", g_view.clientAddr.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("   %.1f fps   %.1f KB/s",
                            g_view.fps.load(), g_view.bandwidthKBs.load());
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f,0.55f,0.55f,1.0f));
        ImGui::Text("○  Waiting for client on port %d", tvproto::DEFAULT_PORT);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // ── Side panel width ─────────────────────────────────────────────────────
    const float SIDE_W   = g_view.clientConnected ? 220.0f : 0.0f;
    const float CONT_W   = W - SIDE_W;
    const float CONT_H   = H - BAR_H;

    // ── Main content ─────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos({0, BAR_H});
    ImGui::SetNextWindowSize({CONT_W, CONT_H});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f,0.043f,0.043f,1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::Begin("##content", nullptr, ROOT | ImGuiWindowFlags_NoScrollWithMouse);

    if (!g_view.clientConnected) {
        // Waiting screen
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 center = {winPos.x + CONT_W * 0.5f, winPos.y + CONT_H * 0.5f};
        drawSpinner(center, 36.0f, t);

        auto textCenter = [&](const char* s, float yOff, ImVec4 col = {1,1,1,1}) {
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::SetCursorPos({(CONT_W - ImGui::CalcTextSize(s).x) * 0.5f,
                                  CONT_H * 0.5f + yOff});
            ImGui::Text("%s", s);
            ImGui::PopStyleColor();
        };
        textCenter("Waiting for connection...", 56.0f);
        textCenter("Give your IP to the remote user and have them", 82.0f,
                   {0.45f,0.45f,0.45f,1.0f});
        textCenter("start  Tesla Remote Client.", 100.0f,
                   {0.45f,0.45f,0.45f,1.0f});
        char portLine[64];
        snprintf(portLine, sizeof(portLine), "Listening on port  %d", tvproto::DEFAULT_PORT);
        textCenter(portLine, 128.0f, {0.908f,0.129f,0.153f,0.85f});

    } else if (texId && texW > 0 && texH > 0) {
        float scX = CONT_W / static_cast<float>(texW);
        float scY = CONT_H / static_cast<float>(texH);
        float sc  = std::min(scX, scY);
        float iw  = texW * sc;
        float ih  = texH * sc;
        float ox  = (CONT_W - iw) * 0.5f;
        float oy  = (CONT_H - ih) * 0.5f;
        ImGui::SetCursorPos({ox, oy});
        ImGui::Image(
            (ImTextureID)(ImU64)texId,
            {iw, ih});
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // ── Side panel ───────────────────────────────────────────────────────────
    if (g_view.clientConnected) {
        ImGui::SetNextWindowPos({CONT_W, BAR_H});
        ImGui::SetNextWindowSize({SIDE_W, CONT_H});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.062f,0.062f,0.062f,1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0f, 14.0f});
        ImGui::Begin("##side", nullptr, ROOT);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.908f,0.129f,0.153f,1.0f));
        ImGui::Text("Session Info");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Client");
        ImGui::Text("%s", g_view.clientAddr.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Resolution");
        ImGui::Text("%d x %d", texW, texH);
        ImGui::Spacing();
        ImGui::TextDisabled("Frames received");
        ImGui::Text("%d", g_view.totalFrames.load());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.908f,0.129f,0.153f,1.0f));
        ImGui::Text("Performance");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("FPS");
        ImGui::Text("%.1f", g_view.fps.load());

        float fpsMax = *std::max_element(s_fpsHist, s_fpsHist + 90);
        if (fpsMax < 1.0f) fpsMax = 60.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.908f,0.129f,0.153f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,   ImVec4(0.08f, 0.08f,0.08f, 1.0f));
        ImGui::PlotLines("##fps", s_fpsHist, 90, s_fpsIdx % 90,
                         nullptr, 0.0f, fpsMax, {SIDE_W - 28.0f, 50.0f});
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::TextDisabled("Bandwidth");
        ImGui::Text("%.1f KB/s", g_view.bandwidthKBs.load());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button,        {0.18f,0.18f,0.18f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f,0.28f,0.28f,1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f,0.10f,0.10f,1.0f});
        if (ImGui::Button("End Session", {SIDE_W - 28.0f, 36.0f}))
            g_view.shouldStop = true;
        ImGui::PopStyleColor(3);

        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main() {
    glfwSetErrorCallback([](int, const char*) {});
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* win = glfwCreateWindow(
        1280, 800, "Tesla Remote – Viewer", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f);
    if (!font) io.Fonts->AddFontDefault();

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    applyTeslaTheme();

    std::thread net(networkThread);

    GLuint texId = 0;
    int    texW  = 0;
    int    texH  = 0;
    std::vector<uint8_t> localFrame;

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // Pull new frame (minimal lock)
        if (g_view.newFrame.exchange(false)) {
            std::lock_guard<std::mutex> lk(g_view.frameMutex);
            localFrame = g_view.frameData;
            texW       = g_view.frameW;
            texH       = g_view.frameH;
        }

        // Upload to GPU (must be on GL thread)
        if (!localFrame.empty() && texW > 0 && texH > 0) {
            if (texId == 0) glGenTextures(1, &texId);
            glBindTexture(GL_TEXTURE_2D, texId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, texW, texH, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, localFrame.data());
            localFrame.clear();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUI(texId, texW, texH);

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.043f, 0.043f, 0.043f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    g_view.shouldStop = true;
    net.join();

    if (texId) glDeleteTextures(1, &texId);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
