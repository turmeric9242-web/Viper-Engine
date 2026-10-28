#ifndef VIPER_ENGINE_H
#define VIPER_ENGINE_H
/* Editor + game loop. scene_path NULL/empty => open the project browser GUI first.
   argv0 (how we were launched) is used by "Compile project" to self-copy into exports/.
   play_only != 0 => skip the editor and start the first-person game immediately
   (this is what a compiled/exported game's run.bat passes: `viper.exe game.vscene --play`). */
int engine_run(const char *scene_path, const char *argv0, int play_only);
#endif
