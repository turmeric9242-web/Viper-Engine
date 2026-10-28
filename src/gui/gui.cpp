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

        bool have = m->projsel[0] != 0;
        if (ImGui::Button("Open project") && have) m->open_pressed = 1;
        ImGui::SameLine();
        if (ImGui::Button("Compile project (standalone .exe)") && have) m->export_pressed = 1;
        if (!have) ImGui::TextDisabled("select a project above first");

        ImGui::TextWrapped("%s", m->status[0] ? m->status : "pick a project, or type a name and press New project");
        ImGui::TextDisabled("ESC quits | in the editor: M returns here, F5 plays, F2 saves");
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

extern "C" void gui_editor_frame(GuiEditor *ed) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    /* ---------- Hierarchy (left) ---------- */
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(280, 720), ImGuiCond_Appearing);
    ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::Text("%s (%d entities)", ed->scene->name, ed->scene->count);
    if (ImGui::Button("+ cube")) {
        Entity *e = scene_add(ed->scene, "Cube");
        if (e) { e->mesh = MESH_CUBE; e->body = BODY_STATIC; }
    }
    ImGui::SameLine();
    if (ImGui::Button("+ sphere")) {
        Entity *e = scene_add(ed->scene, "Sphere");
        if (e) { e->mesh = MESH_SPHERE; e->body = BODY_DYNAMIC; }
    }
    for (int i = 0; i < ed->scene->count; i++) {
        Entity *e = &ed->scene->e[i];
        if (!e->active) continue;
        char label[64]; snprintf(label, sizeof label, "%s##%d", e->name, i);
        if (ImGui::Selectable(label, ed->selected == i)) ed->selected = i;
    }
    ImGui::End();

    /* ---------- Inspector (right) ---------- */
    ImGui::SetNextWindowPos(ImVec2(1000, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(280, 720), ImGuiCond_Appearing);
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (ed->selected < 0 || ed->selected >= ed->scene->count || !ed->scene->e[ed->selected].active) {
        ImGui::TextDisabled("no selection");
    } else {
        Entity *e = &ed->scene->e[ed->selected];
        ImGui::InputText("name", e->name, sizeof e->name);
        ImGui::DragFloat3("pos", &e->pos.x, 0.1f);
        ImGui::DragFloat3("rot", &e->rot.x, 1.0f);
        ImGui::DragFloat3("scale", &e->scale.x, 0.05f);
        ImGui::ColorEdit3("color", &e->color.x);
        int mesh = e->mesh;
        if (ImGui::Combo("mesh", &mesh, "none\0cube\0sphere\0obj\0")) e->mesh = (MeshType)mesh;
        int body = e->body;
        if (ImGui::Combo("body", &body, "none\0static\0dynamic\0")) e->body = (BodyType)body;
        bool chk = e->checker != 0;
        if (ImGui::Checkbox("checker", &chk)) e->checker = chk ? 1 : 0;
        ImGui::InputText("script", e->script, sizeof e->script);
        ImGui::InputInt("controller", &e->controller);
        ImGui::DragFloat("follow dist", &e->follow_dist, 0.1f);
        ImGui::DragFloat("follow height", &e->follow_height, 0.1f);
        if (e->mesh == MESH_OBJ) {
            ImGui::InputText("mesh path", e->mesh_path, sizeof e->mesh_path);
            ImGui::InputText("tex path", e->tex_path, sizeof e->tex_path);
        }
        if (ImGui::Button("Save scene (F2)")) ed->save_pressed = 1;
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) ed->duplicate_pressed = 1;
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ed->delete_pressed = 1;
    }
    if (ed->status[0]) ImGui::TextWrapped("%s", ed->status);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
