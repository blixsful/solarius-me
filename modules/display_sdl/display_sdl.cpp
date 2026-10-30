#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>
#include <stdexcept>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"
#include "imgui.h"
#include "machine.hpp"
#include "scancode_map.hpp"

namespace solme {

namespace {

enum class FitMode { Auto, Best, Fixed };

struct Preset {
    const char* label;
    float zoom;
};

const Preset kPresets[] = {
    {"720x400 (1x)", 1.0f},
    {"1080x600 (1.5x)", 1.5f},
    {"1440x800 (2x)", 2.0f},
    {"2160x1200 (3x)", 3.0f},
};

}

class DisplaySdl : public Device {
public:
    DisplaySdl(Machine& m, const Props& props) : machine(m) {
        zoom = std::strtof(props.str("zoom", "1").c_str(), nullptr);
        if (zoom < 0.5f) {
            zoom = 1.0f;
        }
        std::string fit = props.str("fit", "auto");
        mode = fit == "best" ? FitMode::Best : (fit == "fixed" ? FitMode::Fixed : FitMode::Auto);
        if (props.has("zoom") && !props.has("fit") && zoom != 1.0f) {
            mode = FitMode::Fixed;
        }
        parseScript(props.str("script"));

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }
        SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
        int width = static_cast<int>(720 * (mode == FitMode::Fixed ? zoom : 1.0f));
        int height = static_cast<int>(400 * (mode == FitMode::Fixed ? zoom : 1.0f)) + kMenuGuess;
        window = SDL_CreateWindow("SolME Display", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                  SDL_WINDOW_RESIZABLE);
        if (!window) {
            throw std::runtime_error(std::string("cannot create window: ") + SDL_GetError());
        }
        SDL_SetWindowMinimumSize(window, 320, 200);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!renderer) {
            throw std::runtime_error(std::string("cannot create renderer: ") + SDL_GetError());
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);

        machine.setInteractive(true);
        machine.addPoller([this]() { pump(); });
        lastStatTime = std::chrono::steady_clock::now();
    }

    ~DisplaySdl() override { shutdown(); }

    void shutdown() override {
        if (!window) {
            return;
        }
        if (texture) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        window = nullptr;
        renderer = nullptr;
        SDL_Quit();
    }

private:
    static constexpr int kMenuGuess = 19;

    struct ScriptStep {
        std::string op;
        std::string arg;
    };

    void parseScript(const std::string& text) {
        if (text.empty()) {
            return;
        }
        for (const auto& part : splitString(text, ';')) {
            size_t colon = part.find(':');
            ScriptStep step;
            step.op = colon == std::string::npos ? part : part.substr(0, colon);
            step.arg = colon == std::string::npos ? "" : part.substr(colon + 1);
            script.push_back(step);
        }
    }

    void runScriptStep() {
        if (scriptIndex >= script.size()) {
            return;
        }
        if (scriptWait > 0) {
            --scriptWait;
            return;
        }
        const ScriptStep& step = script[scriptIndex++];
        if (step.op == "move") {
            int x = 0;
            int y = 0;
            std::sscanf(step.arg.c_str(), "%d,%d", &x, &y);
            SDL_Event e{};
            e.type = SDL_MOUSEMOTION;
            e.motion.x = x;
            e.motion.y = y;
            SDL_PushEvent(&e);
        } else if (step.op == "down" || step.op == "up") {
            SDL_Event e{};
            e.type = step.op == "down" ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.state = step.op == "down" ? SDL_PRESSED : SDL_RELEASED;
            e.button.x = lastMouseX;
            e.button.y = lastMouseY;
            SDL_PushEvent(&e);
        } else if (step.op == "wait") {
            scriptWait = std::atoi(step.arg.c_str());
        } else if (step.op == "shot") {
            pendingShot = step.arg;
        } else if (step.op == "key") {
            SDL_Event e{};
            e.type = SDL_KEYDOWN;
            e.key.keysym.scancode = static_cast<SDL_Scancode>(std::atoi(step.arg.c_str()));
            e.key.state = SDL_PRESSED;
            SDL_PushEvent(&e);
            e.type = SDL_KEYUP;
            e.key.state = SDL_RELEASED;
            SDL_PushEvent(&e);
        } else if (step.op == "quit") {
            machine.requestExit(0, "display script finished");
        }
    }

    void sendScancodes(int sdlScancode, bool pressed) {
        auto& queue = machine.scancodes();
        ScancodeSeq seq = sdlToSet1(sdlScancode);
        if (seq.length == 0) {
            return;
        }
        if (seq.extended) {
            queue.push_back(0xE0);
        }
        queue.push_back(static_cast<u8>(pressed ? seq.code : (seq.code | 0x80)));
    }

    void releaseAllKeys() {
        for (int code : held) {
            sendScancodes(code, false);
        }
        held.clear();
    }

    void handleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            ImGuiIO& io = ImGui::GetIO();
            switch (event.type) {
                case SDL_QUIT:
                    machine.requestExit(0, "display window closed");
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        releaseAllKeys();
                    }
                    break;
                case SDL_MOUSEMOTION:
                    lastMouseX = event.motion.x;
                    lastMouseY = event.motion.y;
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    if (io.WantCaptureKeyboard) {
                        break;
                    }
                    bool down = event.type == SDL_KEYDOWN;
                    int code = event.key.keysym.scancode;
                    if (down) {
                        held.insert(code);
                    } else {
                        held.erase(code);
                    }
                    sendScancodes(code, down);
                    break;
                }
                default: break;
            }
        }
    }

    void ensureTexture(const VideoFrame& frame) {
        if (texture && (textureW != frame.width || textureH != frame.height)) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        if (!texture) {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, frame.width,
                                        frame.height);
            textureW = frame.width;
            textureH = frame.height;
            textureGen = ~0ULL;
        }
        if (textureGen != frame.generation) {
            SDL_UpdateTexture(texture, nullptr, frame.pixels, frame.width * 4);
            textureGen = frame.generation;
        }
    }

    void applyZoomWindow(float newZoom) {
        mode = FitMode::Fixed;
        zoom = newZoom;
        SDL_SetWindowSize(window, static_cast<int>(720 * zoom), static_cast<int>(400 * zoom) + menuHeight);
    }

    void drawMenus() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }
        if (ImGui::BeginMenu("Machine")) {
            if (ImGui::MenuItem(machine.isPaused() ? "Resume" : "Pause")) {
                machine.setPaused(!machine.isPaused());
                SDL_SetWindowTitle(window, machine.isPaused() ? "SolME Display (paused)" : "SolME Display");
            }
            if (ImGui::MenuItem("Stop")) {
                machine.requestExit(0, "stopped from display menu");
            }
            if (ImGui::MenuItem("Info")) {
                showInfo = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Debug", nullptr, &showDebug);
            if (ImGui::BeginMenu("Display")) {
                if (ImGui::MenuItem("Auto zoom to fit", nullptr, mode == FitMode::Auto)) {
                    mode = FitMode::Auto;
                }
                if (ImGui::MenuItem("Best fit", nullptr, mode == FitMode::Best)) {
                    mode = FitMode::Best;
                }
                if (ImGui::BeginMenu("Resolution")) {
                    for (const auto& preset : kPresets) {
                        bool selected = mode == FitMode::Fixed && std::fabs(zoom - preset.zoom) < 0.01f;
                        if (ImGui::MenuItem(preset.label, nullptr, selected)) {
                            applyZoomWindow(preset.zoom);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        menuHeight = static_cast<int>(ImGui::GetFrameHeight());
        ImGui::EndMainMenuBar();
    }

    std::string cpuText() {
        std::ostringstream out;
        machine.cpu().dumpState(out);
        return out.str();
    }

    void drawInfo() {
        if (!showInfo) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Machine Info", &showInfo)) {
            std::string text = machine.describe();
            ImGui::TextUnformatted(text.c_str());
            ImGui::Separator();
            ImGui::Text("CPU clock: %.2f MHz", machine.cpu().frequencyHz() / 1e6);
            ImGui::Text("Instructions: %llu", static_cast<unsigned long long>(machine.cpu().instructionCount()));
            ImGui::Text("Virtual time: %.3f s", machine.scheduler().now() / 1e9);
            ImGui::Text("State: %s", machine.isPaused() ? "paused" : "running");
            ImGui::Separator();
            ImGui::Text("Guest video: %dx%d", textureW, textureH);
            ImGui::Text("Window: %dx%d", winW, winH);
        }
        ImGui::End();
    }

    void drawDebug() {
        if (!showDebug) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Debug", &showDebug)) {
            if (ImGui::BeginTabBar("debug-tabs")) {
                if (ImGui::BeginTabItem("CPU")) {
                    std::string text = cpuText();
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::Text("Instructions: %llu", static_cast<unsigned long long>(machine.cpu().instructionCount()));
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Memory")) {
                    ImGui::InputScalar("Address", ImGuiDataType_U32, &memAddress, nullptr, nullptr, "%05X",
                                       ImGuiInputTextFlags_CharsHexadecimal);
                    AddressSpace& mem = machine.space("mem");
                    for (int row = 0; row < 16; ++row) {
                        char line[128];
                        int len = std::snprintf(line, sizeof line, "%05X:", (memAddress + row * 16) & 0xFFFFF);
                        for (int col = 0; col < 16; ++col) {
                            len += std::snprintf(line + len, sizeof line - len, " %02X",
                                                 mem.read8((memAddress + row * 16 + col) & 0xFFFFF));
                        }
                        ImGui::TextUnformatted(line);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Performance")) {
                    ImGui::Text("Display: %.1f fps", fps);
                    ImGui::Text("Emulation: %.2f MIPS", mips);
                    ImGui::Text("Virtual/real time: %.2fx", speed);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void updateStats() {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - lastStatTime).count();
        ++frames;
        if (dt >= 0.5) {
            fps = frames / dt;
            u64 instr = machine.cpu().instructionCount();
            u64 virt = machine.scheduler().now();
            mips = (instr - lastInstr) / dt / 1e6;
            speed = (virt - lastVirtual) / 1e9 / dt;
            lastInstr = instr;
            lastVirtual = virt;
            lastStatTime = now;
            frames = 0;
        }
    }

    void saveShot(const std::string& path) {
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, winW, winH, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!surface) {
            return;
        }
        SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
        SDL_SaveBMP(surface, path.c_str());
        SDL_FreeSurface(surface);
    }

    void renderFrame() {
        SDL_GetWindowSize(window, &winW, &winH);
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        drawMenus();
        drawInfo();
        drawDebug();
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 16, 16, 16, 255);
        SDL_RenderClear(renderer);

        VideoFrame frame;
        if (!machine.videos().empty() && machine.videos()[0]->captureFrame(frame)) {
            ensureTexture(frame);
            float availW = static_cast<float>(winW);
            float availH = static_cast<float>(winH - menuHeight);
            float scale = 1.0f;
            float fitScale = std::min(availW / frame.width, availH / frame.height);
            bool smooth = false;
            if (mode == FitMode::Auto) {
                scale = fitScale;
                smooth = true;
            } else if (mode == FitMode::Best) {
                scale = fitScale >= 1.0f ? std::floor(fitScale) : fitScale;
                smooth = fitScale < 1.0f;
            } else {
                scale = zoom;
                smooth = std::fabs(zoom - std::round(zoom)) > 0.01f;
            }
            SDL_SetTextureScaleMode(texture, smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
            SDL_FRect dst;
            dst.w = frame.width * scale;
            dst.h = frame.height * scale;
            dst.x = std::floor((availW - dst.w) / 2.0f);
            dst.y = menuHeight + std::floor((availH - dst.h) / 2.0f);
            SDL_RenderCopyF(renderer, texture, nullptr, &dst);
        }
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (!pendingShot.empty()) {
            saveShot(pendingShot);
            pendingShot.clear();
        }
        SDL_RenderPresent(renderer);
        updateStats();
    }

    void pump() {
        if (!window) {
            return;
        }
        handleEvents();
        auto now = std::chrono::steady_clock::now();
        if (now - lastRender < std::chrono::milliseconds(16)) {
            return;
        }
        lastRender = now;
        runScriptStep();
        renderFrame();
    }

    Machine& machine;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int textureW = 0;
    int textureH = 0;
    u64 textureGen = ~0ULL;
    FitMode mode = FitMode::Auto;
    float zoom = 1.0f;
    int menuHeight = kMenuGuess;
    int winW = 0;
    int winH = 0;
    bool showInfo = false;
    bool showDebug = false;
    u32 memAddress = 0;
    std::set<int> held;
    std::vector<ScriptStep> script;
    size_t scriptIndex = 0;
    int scriptWait = 0;
    std::string pendingShot;
    int lastMouseX = 0;
    int lastMouseY = 0;
    std::chrono::steady_clock::time_point lastRender{};
    std::chrono::steady_clock::time_point lastStatTime{};
    u64 frames = 0;
    u64 lastInstr = 0;
    u64 lastVirtual = 0;
    double fps = 0;
    double mips = 0;
    double speed = 0;
};

SOLME_REGISTER_DEVICE("display-sdl", DisplaySdl);

}
