// The code of the libraries of one file each that the game uses outside Windows (third_party/): built once, here.
// stb_image reads the pictures behind the game (.png .jpg .bmp), stb_truetype draws the text, dr_mp3, dr_wav and
// dr_flac read the music.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "../third_party/stb/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb/stb_truetype.h"
#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_libs/dr_flac.h"
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_libs/dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#include "../third_party/dr_libs/dr_wav.h"
