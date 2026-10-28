#ifndef VIPER_ENGINE_H
#define VIPER_ENGINE_H
/* Editor + game loop. scene_path NULL/empty => open the project browser GUI first.
   argv0 (how we were launched) is used by "Compile project" to self-copy into exports/. */
int engine_run(const char *scene_path, const char *argv0);
#endif
