--add_options("SDL2", "SDL2_mixer")

add_defines("SDL_MAIN_HANDLED")
--add_defines("SDL_RDR_SOFTWARE")
add_defines("ENABLE_SL6")

set_kind("binary")
set_targetdir(".")
add_includedirs(".")
add_ldflags("-std=c11 -lSDL2 -lwinmm -limm32 -lole32 -loleaut32 -lversion -lopengl32 -lgdi32 -lgdiplus -lsetupapi -lcomdlg32", "-static")
add_ldflags("-mwindows")
--add_ldflags("-mconsole")

target("reinetteIIplus_win32") 
	add_files("reinetteII+.rc")
	add_files("./reinetteII+.c", "./puce6502.c")
