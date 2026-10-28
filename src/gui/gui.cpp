#include "gui/gui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <string.h>

static int g_active;

extern "C" int gui_init(void *glfw_window) {
    IMGUI_CHECKVERSION();
    g_active = 1;
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL((GLFWwindow *)glfw_window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    return 1;
}

extern "C" void gui_shutdown(void) {
    if (!g_active) return;
    g_active = 0;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

extern "C" void gui_frame(GuiMenu *m) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(1200, 620), ImGuiCond_Appearing);
    if (ImGui::Begin("Viper Engine project browser", nullptr, ImGuiWindowFlags_None)) {
        ImGui::Text("Projects under projects/: %d", m->nprojects);
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) m->refresh_pressed = 1;

        ImGui::BeginChild("projectlist", ImVec2(0, 300), true);
        for (int i = 0; i < m->nprojects; i++) {
            bool sel = strcmp(m->projsel, m->projects[i]) == 0;
            if (ImGui::Selectable(m->projects[i], sel))
                snprintf(m->projsel, sizeof m->projsel, "%s", m->projects[i]);
        }
        ImGui::EndChild();

        ImGui::InputTextWithHint("##newname", "MyGame", m->newname, sizeof m->newname);
        ImGui::SameLine();
        if (ImGui::Button("New project")) m->new_pressed = 1;

        {
            bool have = m->projsel[0] != 0;
            if (!have) {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            }
            if (ImGui::Button("Open project")) m->open_pressed = 1;
            ImGui::SameLine();
            if (ImGui::Button("Compile project (standalone .exe)")) m->export_pressed = 1;
            if (!have) {
                ImGui::PopItemFlag();
                ImGui::PopStyleVar();
            }
        }

        ImGui::TextWrapped("%s", m->status[0] ? m->status : "pick a project, or type a name and press New project");
        ImGui::TextDisabled("ESC quits | in the editor: M returns here, F5 plays, F2 saves");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
