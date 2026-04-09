#
# STB image library (https://github.com/nothings/stb)
# Some filesystem adjustments have been made + C source files have been
#  added for convenience of use within this code base.
add_library(stb "include/stb/stb_image.h" "include/stb/stb_image_resize2.h" "src/stb_image.c" "src/stb_image_resize2.c")
set_target_properties(stb PROPERTIES CXX_STANDARD 23)
target_include_directories(stb PUBLIC include)
