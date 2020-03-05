/*-----------------------------------------------------------------

 Copyright (C) 2010  Dave "WinterMute" Murphy

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

------------------------------------------------------------------*/

#include <nds.h>
#include <fat.h>
#include <limits.h>
#include <nds/fifocommon.h>

#include <stdio.h>
#include <stdarg.h>

#include "nds_loader_arm9.h"

#include "bios_decompress_callback.h"

#include "inifile.h"

#include "topLoad.h"
#include "subLoad.h"
#include "topError.h"
#include "subError.h"

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24

#define TMD_SIZE        0x208

bool gotoSettings = false;

bool splash = true;
bool dsiSplash = false;
bool titleAutoboot = false;

bool topSplashFound = true;
bool bottomSplashFound = true;

void vramcpy_ui (void* dest, const void* src, int size) 
{
	u16* destination = (u16*)dest;
	u16* source = (u16*)src;
	while (size > 0) {
		*destination++ = *source++;
		size-=2;
	}
}

void BootSplashInit() {

	if (topSplashFound || bottomSplashFound) {
		// Do nothing
	} else {
		videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
		videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);
		vramSetBankA (VRAM_A_MAIN_BG_0x06000000);
		vramSetBankC (VRAM_C_SUB_BG_0x06200000);
		REG_BG0CNT = BG_MAP_BASE(0) | BG_COLOR_256 | BG_TILE_BASE(2);
		REG_BG0CNT_SUB = BG_MAP_BASE(0) | BG_COLOR_256 | BG_TILE_BASE(2);
		BG_PALETTE[0]=0;
		BG_PALETTE[255]=0xffff;
		u16* bgMapTop = (u16*)SCREEN_BASE_BLOCK(0);
		u16* bgMapSub = (u16*)SCREEN_BASE_BLOCK_SUB(0);
		for (int i = 0; i < CONSOLE_SCREEN_WIDTH*CONSOLE_SCREEN_HEIGHT; i++) {
			bgMapTop[i] = (u16)i;
			bgMapSub[i] = (u16)i;
		}
	}

}

static u16 bmpImageBuffer[0x18000/2];
static u16 outputImageBuffer[2][0x18000/2];

void LoadBMP(bool top) {
	FILE* file = fopen((top ? "sd:/hiya/splashtop.bmp" : "sd:/hiya/splashbottom.bmp"), "rb");

	if (!file) return;

	// Start loading
	fseek(file, 0xe, SEEK_SET);
	u8 pixelStart = (u8)fgetc(file) + 0xe;
	fseek(file, pixelStart, SEEK_SET);
	fread(bmpImageBuffer, 1, 0x18000, file);
	u16* src = bmpImageBuffer;
	int x = 0;
	int y = 191;
	for (int i=0; i<256*192; i++) {
		if (x >= 256) {
			x = 0;
			y--;
		}
		u16 val = *(src++);
		outputImageBuffer[top][y*256+x] = ((val>>10)&0x1f) | ((val)&(0x1f<<5)) | (val&0x1f)<<10 | BIT(15);
		x++;
	}

	fclose(file);
}

void LoadScreen() {
	if (topSplashFound || bottomSplashFound) {
		consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, true, true);
		consoleClear();
		consoleInit(NULL, 1, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
		consoleClear();

		LoadBMP(true);
		LoadBMP(false);

		if (topSplashFound) {
			// Set up background
			videoSetMode(MODE_3_2D | DISPLAY_BG3_ACTIVE);
			vramSetBankD(VRAM_D_MAIN_BG_0x06040000);
			REG_BG3CNT = BG_MAP_BASE(0) | BG_BMP16_256x256;
			REG_BG3X = 0;
			REG_BG3Y = 0;
			REG_BG3PA = 1<<8;
			REG_BG3PB = 0;
			REG_BG3PC = 0;
			REG_BG3PD = 1<<8;

			dmaCopyWordsAsynch(0, outputImageBuffer[1], BG_GFX, 0x18000);
		}
		if (bottomSplashFound) {
			// Set up background
			videoSetModeSub(MODE_3_2D | DISPLAY_BG3_ACTIVE);
			vramSetBankC (VRAM_C_SUB_BG_0x06200000);
			REG_BG3CNT_SUB = BG_MAP_BASE(0) | BG_BMP16_256x256;
			REG_BG3X_SUB = 0;
			REG_BG3Y_SUB = 0;
			REG_BG3PA_SUB = 1<<8;
			REG_BG3PB_SUB = 0;
			REG_BG3PC_SUB = 0;
			REG_BG3PD_SUB = 1<<8;

			dmaCopyWordsAsynch(1, outputImageBuffer[0], BG_GFX_SUB, 0x18000);
		}
	} else {
		// Display Load Screen
		swiDecompressLZSSVram ((void*)topLoadTiles, (void*)CHAR_BASE_BLOCK(2), 0, &decompressBiosCallback);
		swiDecompressLZSSVram ((void*)subLoadTiles, (void*)CHAR_BASE_BLOCK_SUB(2), 0, &decompressBiosCallback);
		vramcpy_ui (&BG_PALETTE[0], topLoadPal, topLoadPalLen);
		vramcpy_ui (&BG_PALETTE_SUB[0], subLoadPal, subLoadPalLen);
	}
}

const char* settingsinipath = "sd:/hiya/settings.ini";

int cursorPosition = 0;

void LoadSettings(void) {
	// GUI
	CIniFile settingsini( settingsinipath );

	splash = settingsini.GetInt("HIYA-CFW", "SPLASH", 0);
	dsiSplash = settingsini.GetInt("HIYA-CFW", "DSI_SPLASH", 0);
	titleAutoboot = settingsini.GetInt("HIYA-CFW", "TITLE_AUTOBOOT", 0);
}

void SaveSettings(void) {
	// GUI
	CIniFile settingsini( settingsinipath );

	settingsini.SetInt("HIYA-CFW", "SPLASH", splash);
	settingsini.SetInt("HIYA-CFW", "DSI_SPLASH", dsiSplash);
	settingsini.SetInt("HIYA-CFW", "TITLE_AUTOBOOT", titleAutoboot);
	settingsini.SaveIniFile(settingsinipath);
}

void setupConsole() {
	// Subscreen as a console
	videoSetMode(MODE_0_2D);
	vramSetBankG(VRAM_G_MAIN_BG);
	videoSetModeSub(MODE_0_2D);
	vramSetBankH(VRAM_H_SUB_BG);
}

int main( int argc, char **argv) {

	// defaultExceptionHandler();

	if (fatInitDefault()) {

		LoadSettings();
	
		gotoSettings = (access("sd:/hiya/settings.ini", F_OK) != 0);
	
		scanKeys();

		if(keysHeld() & KEY_SELECT) gotoSettings = true;

		if(gotoSettings) {
			setupConsole();

			int pressed = 0;
			bool menuprinted = true;

			while(1) {
				if(menuprinted) {
					consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, true, true);
					consoleClear();

					printf ("\x1B[46m");

					printf("HiyaCFW v1.3.3 configuration\n");
					printf("Press A to select, START to save");
					printf("\n");

					printf ("\x1B[47m");

					if(cursorPosition == 0) printf ("\x1B[41m");
					else printf ("\x1B[47m");

					printf(" Splash: ");
					if(splash)
						printf("Off( ), On(x)");
					else
						printf("Off(x), On( )");
					printf("\n\n");
					
					if(cursorPosition == 1) printf ("\x1B[41m");
					else printf ("\x1B[47m");

					if(dsiSplash)
						printf(" (x)");
					else
						printf(" ( )");
					printf(" DSi Splash/H&S screen\n");

					if(cursorPosition == 2) printf ("\x1B[41m");
					else printf ("\x1B[47m");

					if(titleAutoboot)
						printf(" (x)");
					else
						printf(" ( )");
					printf(" Autoboot title\n");

					consoleInit(NULL, 1, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
					consoleClear();

					printf("\n");
					if(cursorPosition == 0) {
						printf(" Enable splash screen.");
					} else if(cursorPosition == 1) {
						printf(" Enable showing the DSi Splash/\n");
						printf(" Health & Safety screen.");
					} else if(cursorPosition == 2) {
						printf(" Load title contained in\n");
						printf(" sd:/hiya/autoboot.bin\n");
						printf(" instead of the DSi Menu.");
					}

					menuprinted = false;
				}

				do {
					scanKeys();
					pressed = keysDownRepeat();
					swiWaitForVBlank();
				} while (!pressed);

				if (pressed & KEY_L) {
					// Debug code
					FILE* ResetData = fopen("sd:/hiya/ResetData_extract.bin","wb");
					fwrite((void*)0x02000000,1,0x800,ResetData);
					fclose(ResetData);
					for (int i = 0; i < 30; i++) swiWaitForVBlank();
				}

				if (pressed & KEY_A) {
					switch(cursorPosition){
						case 0:
						default:
							splash = !splash;
							break;
						case 1:
							dsiSplash = !dsiSplash;
							break;
						case 2:
							titleAutoboot = !titleAutoboot;
							break;
					}
					menuprinted = true;
				}

				if (pressed & KEY_UP) {
					cursorPosition--;
					menuprinted = true;
				} else if (pressed & KEY_DOWN) {
					cursorPosition++;
					menuprinted = true;
				}

				if (cursorPosition < 0) cursorPosition = 2;
				if (cursorPosition > 2) cursorPosition = 0;

				if (pressed & KEY_START) {
					SaveSettings();
					break;
				}
			}
		}

		if (!gotoSettings && (*(u32*)0x02000300 == 0x434E4C54)) {
			// if "CNLT" is found, then don't show splash
			splash = false;
		}

		if ((*(u32*)0x02000300 == 0x434E4C54) && (*(u32*)0x02000310 != 0x00000000)) {
			// if "CNLT" is found, and a title is set to launch, then don't autoboot title in "autoboot.bin"
			titleAutoboot = false;
		}

		if (titleAutoboot) {
			FILE* ResetData = fopen("sd:/hiya/autoboot.bin","rb");
			if (ResetData) {
				fread((void*)0x02000300,1,0x20,ResetData);
				dsiSplash = false;	// Disable DSi splash, so that DSi Menu doesn't appear
			}
			fclose(ResetData);
		}

		if (!dsiSplash) {
			fifoSendValue32(FIFO_USER_03, 1);
			// Tell Arm7 to check FIFO_USER_03 code	
			fifoSendValue32(FIFO_USER_04, 1);
			// Small delay to ensure arm7 has time to write i2c stuff
			for (int i = 0; i < 1*3; i++) { swiWaitForVBlank(); }
		} else {
			fifoSendValue32(FIFO_USER_04, 1);
		}

		if (splash) {

			if (access("sd:/hiya/splashtop.bmp", F_OK)) topSplashFound = false;
			if (access("sd:/hiya/splashbottom.bmp", F_OK)) bottomSplashFound = false;

			BootSplashInit();

			LoadScreen();
			
			for (int i = 0; i < 60*3; i++) { swiWaitForVBlank(); }
		}

		char tmdpath[256];
		for (u8 i = 0x41; i <= 0x5A; i++) {
			snprintf (tmdpath, sizeof(tmdpath), "sd:/title/00030017/484e41%x/content/title.tmd", i);
			if (access(tmdpath, F_OK)) {} else { break; }
		}
		FILE* f_tmd = fopen(tmdpath, "rb");
		off_t fsize = 0;
		if (f_tmd) {
			fseek(f_tmd, 0, SEEK_END);
			fsize = ftell(f_tmd);
			if (fsize && (fsize <= TMD_SIZE)) {
				int err = runNdsFile("/hiya/BOOTLOADER.NDS", 0, NULL);
				setupConsole();
				consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, true, true);
				consoleClear();
				iprintf ("Start failed. Error %i\n", err);
				if (err == 1) printf ("bootloader.nds not found!");
				consoleInit(NULL, 1, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
				consoleClear();
			} else {
				fsize = 0;
				setupConsole();
				consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, true, true);
				consoleClear();
				printf("Error!\n");
				printf("Launcher's .tmd is too big.\n");
				printf("\n");
				printf("Please replace it with a clean\n");
				printf("one.\n");
				consoleInit(NULL, 1, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
				consoleClear();
			}
			fclose(f_tmd);
		} else {
			setupConsole();
			consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 15, 0, true, true);
			consoleClear();
			printf("Error!\n");
			printf("Failed to read Launcher's\n");
			printf("title.tmd\n");
			consoleInit(NULL, 1, BgType_Text4bpp, BgSize_T_256x256, 15, 0, false, true);
			consoleClear();
		}

	} else {

		topSplashFound = false;
		bottomSplashFound = false;

		BootSplashInit();

		// Display Error Screen
		swiDecompressLZSSVram ((void*)topErrorTiles, (void*)CHAR_BASE_BLOCK(2), 0, &decompressBiosCallback);
		swiDecompressLZSSVram ((void*)subErrorTiles, (void*)CHAR_BASE_BLOCK_SUB(2), 0, &decompressBiosCallback);
		vramcpy_ui (&BG_PALETTE[0], topErrorPal, topErrorPalLen);
		vramcpy_ui (&BG_PALETTE_SUB[0], subErrorPal, subErrorPalLen);
	}

	while(1) { swiWaitForVBlank(); }
}

