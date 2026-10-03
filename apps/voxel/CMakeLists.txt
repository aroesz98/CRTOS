# voxel: a 3D block world - a generated island with caves, trees and villages; walk, fly,
# dig and build (software renderer)
crtos_app(voxel
  SOURCES voxel_main.cpp voxel_blocks.cpp voxel_noise.cpp voxel_world.cpp voxel_mesh.cpp voxel_render.cpp voxel_player.cpp
  STACK 32768 HEAP 7340032 LIBS gfx ICON icon.png)
# the renderer's inner loops: all the speed the compiler can give
target_compile_options(app_voxel_objs PRIVATE -O3 -ffast-math -fno-math-errno)
