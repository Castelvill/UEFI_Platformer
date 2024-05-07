#include <Uefi.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimplePointer.h>

CONST unsigned SCREEN_WIDTH = 1024, SCREEN_HEIGHT = 768; //or 800x600
CONST unsigned TILE_SIZE = 40;
CONST int PLAYER_SPEED = 6, JUMP_SPEED = 6, FALL_SPEED = 2;
CONST int PLAYER_JUMP_DURATION = 25;
CONST int ANIMATION_DURATION = 3;
CONST int MONEY_ANIMATION_DURATION = 5;


void clearScreenWithColor(EFI_GRAPHICS_OUTPUT_PROTOCOL* Screen, UINT8 red, UINT8 green, UINT8 blue){
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL backgroundColor;
	backgroundColor.Red = red;
	backgroundColor.Green = green;
	backgroundColor.Blue = blue;
	backgroundColor.Reserved = 0;

	//"The basic graphics operation in the EFI_GRAPHICS_OUTPUT_PROTOCOL is the Block Transfer or Blt. The Blt
	//operation allows data to be read or written to the video adapter’s video memory." ~ UEFI Spec. 2.10., page 426.

	//"Blt a rectangle of pixels on the graphics screen." ~ UEFI Spec. 2.10., page 432.
	Screen->Blt(
		Screen,							//*This - EFI_GRAPHICS_OUTPUT_PROTOCOL,
		&backgroundColor,				//*BltBuffer, OPTIONAL
		EfiBltVideoFill,				//BltOperation,
		0, 0,							//SourceX & SourceY,
		0, 0,							//DestinationX & DestinationY,
		SCREEN_WIDTH, SCREEN_HEIGHT, 	//Width & Height,
		0 								//Delta - ignored when EfiBltVideoFill mode is selected.
	);
}

//Dynamic array of sprites for all game tiles and animations. 
typedef struct {
	//Array of arrays of pixels.
	EFI_GRAPHICS_OUTPUT_BLT_PIXEL** sprites;
} SpriteArray;

//Size of the .bmp image header.
#define BMP_HEADER_SIZE 54
//Modified version of @rubikshift 's "LoadBMP" function.
SpriteArray* loadSprites(EFI_FILE_PROTOCOL* RootDirectory, CHAR16* fileName, int spriteWidth, int spriteHeight, BOOLEAN * imageStatus){
	//Allocate memory for a new SpriteArray.
	SpriteArray * NewSprites = AllocatePool(sizeof(SpriteArray));
	CHAR8 bitmapHeaderBuffer[BMP_HEADER_SIZE];
	UINTN bufferSize = BMP_HEADER_SIZE;

	EFI_FILE_PROTOCOL* SpriteFile;
	//Open the bitmap
	EFI_STATUS OpenStatus = RootDirectory->Open(RootDirectory, &SpriteFile, fileName, EFI_FILE_MODE_READ, 0);
	
	if(EFI_ERROR(OpenStatus)) {
		Print(L"Could not open file \"%s\".\n", fileName);
		*imageStatus = FALSE;
		return NewSprites;
	}
	
	*imageStatus = TRUE;

	//Read the width and height of a new bitmap and allocate memory for its buffer.
	SpriteFile->Read(SpriteFile, &bufferSize, (VOID*) bitmapHeaderBuffer);
	unsigned width = *(unsigned*)&bitmapHeaderBuffer[18];
	unsigned height = *(unsigned*)&bitmapHeaderBuffer[22];
	bufferSize = 3 * width * height * sizeof(CHAR8);
	CHAR8* bitmapBuffer = AllocatePool(bufferSize);

	SpriteFile->Read(SpriteFile, &bufferSize, (VOID*) bitmapBuffer);

	unsigned spriteNumber = width / spriteWidth;

	//Allocate memory for all sprites that will be created by dividing the loaded bitmap into fragments with the same width and height.
	NewSprites->sprites = AllocatePool(spriteNumber * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL*));

	unsigned spriteIdx, inPixelIdx = 0, outPixelIdx = 0, x, y;
	//Copy pixel colors to all sprites.
	for(spriteIdx = 0; spriteIdx < spriteNumber; spriteIdx++){
		//Allocate memory for all pixels in a sprite.
		NewSprites->sprites[spriteIdx] = AllocatePool(spriteWidth * spriteHeight * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
		
		//Copy pixel colors from the buffer to a new sprite.
		for(y = 0; y < spriteHeight; y++){
			for(x = 0; x < spriteWidth; x++){
				//Choose current input pixel by jumping every 3 pixels.
				inPixelIdx = (spriteWidth - 1 - y) * 3 * width + 3 * x + 3 * spriteIdx * spriteWidth;
				outPixelIdx = y * spriteWidth + x;
				NewSprites->sprites[spriteIdx][outPixelIdx].Red = bitmapBuffer[inPixelIdx + 2];
				NewSprites->sprites[spriteIdx][outPixelIdx].Green = bitmapBuffer[inPixelIdx + 1];
				NewSprites->sprites[spriteIdx][outPixelIdx].Blue = bitmapBuffer[inPixelIdx + 0];
				NewSprites->sprites[spriteIdx][outPixelIdx].Reserved = 0;
			}
		}
	}
	
	SpriteFile->Close(SpriteFile);
	FreePool(bitmapBuffer);
	return NewSprites;
}

typedef struct{
	int x, y;
} vec2i;
vec2i rvec2i(int x, int y){
	return (vec2i){x, y};
}

enum ObjectType{
	null, green_brick, red_brick, mossy_brick, web, spider, coin, player
};
typedef struct{
	vec2i pos;
	enum ObjectType type; 
	int frameIdx;			//Currently used sprite from the bitmap. (Bitmaps are divided into sprites with different animation frame or object type.)
	BOOLEAN isActive; 		//Disabled objects are not visible and do not interact with the game.
	BOOLEAN isSolid; 		//Solid blocks create collisions for the player.
} ObjectStruct;
void setupObject(ObjectStruct * Object, vec2i pos, enum ObjectType type, int frameIdx, BOOLEAN isActive, BOOLEAN isSolid){
	Object->pos = pos;
	Object->type = type;
	Object->frameIdx = frameIdx;
	Object->isActive = isActive;
	Object->isSolid = isSolid;
}

typedef struct{
	ObjectStruct Base;
	BOOLEAN direction; //Direction the player is currently facing. FALSE - left, TRUE - right.
	BOOLEAN canJump; //If FALSE, the player has already jumped and has not fallen to the ground yet.
	BOOLEAN isJumping;
	BOOLEAN isFalling;
	BOOLEAN isMoving;
	vec2i momentum;
	int playerJumpTime;
	int animationTime; 
	unsigned coins;
} PlayerStruct;
void setupPlayer(PlayerStruct * Player, vec2i pos, int frameIdx){
	setupObject(&Player->Base, pos, player, frameIdx, TRUE, FALSE);
	Player->Base.type = player;
	Player->Base.isActive = TRUE;
	Player->direction = TRUE;
	Player->canJump = TRUE;
	Player->isJumping = FALSE;
	Player->isFalling = FALSE;
	Player->momentum = rvec2i(0, 0);
	Player->playerJumpTime = 0;
	Player->animationTime = ANIMATION_DURATION;
	Player->coins = 0;
}

typedef struct{
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL * SimpleFileSystemProtocol;
	EFI_FILE_PROTOCOL * RootDirectory;
	EFI_GRAPHICS_OUTPUT_PROTOCOL * Screen;
	SpriteArray * PlayerSprites;
	SpriteArray * BlocksSprites;
	SpriteArray * CoinSprites;
	SpriteArray * CastleSprites;
	SpriteArray * Font;
	SpriteArray * CursorSprite;
	EFI_SIMPLE_POINTER_PROTOCOL * Mouse;
	EFI_EVENT TimerEvent;
	EFI_EVENT events[3];
	BOOLEAN quit;
	BOOLEAN died;
	int coinsAnimationTime;
	int mouseX, mouseY;
	BOOLEAN showMouseCursor;
	BOOLEAN isMouseMoving;
	ObjectStruct * Blocks;
} GameStruct;
EFI_STATUS setupGame(GameStruct * Game){
	EFI_STATUS status;
	UINTN eventId;

	//Locate file system protocol needed to load all game sprites from bitmap files.
	status = gBS->LocateProtocol(
		&gEfiSimpleFileSystemProtocolGuid,			//*Protocol,
		NULL,										//*Registration**Interface OPTIONAL,
		(VOID**) &Game->SimpleFileSystemProtocol	//**Interface
	);
	if(EFI_ERROR(status)){
		Print(L"Error: Simple File System Protocol not found. Press any key to continue.\n");
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
		return EFI_ABORTED;
	}
	
	//Open the root directory on a volume.
	status = Game->SimpleFileSystemProtocol->OpenVolume(
		Game->SimpleFileSystemProtocol,	//*This - EFI_SIMPLE_FILE_SYSTEM_PROTOCOL,
		&Game->RootDirectory			//**Root - EFI_FILE_PROTOCOL
	);
	if(EFI_ERROR(status)){
		Print(L"Error: Opening root director failed. Press any key to continue.\n");
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
		return EFI_ABORTED;
	}

	//Locate EFI_GRAPHICS_OUTPUT_PROTOCOL used for drawing on the screen's frame buffer.
	status = gBS->LocateProtocol(
		&gEfiGraphicsOutputProtocolGuid,
		NULL,
		(VOID **) &Game->Screen
	);
	if(EFI_ERROR(status)){
		Print(L"Error: Graphical Output Protocol not found. Press any key to continue.\n");
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
		return EFI_ABORTED;
	}

	//Load all game sprites from files.
	BOOLEAN imageStatus;
	Game->PlayerSprites = loadSprites(Game->RootDirectory, L"images\\player.bmp", TILE_SIZE, TILE_SIZE, &imageStatus);
	if(!imageStatus){
		return EFI_ABORTED;
	}
	Game->BlocksSprites = loadSprites(Game->RootDirectory, L"images\\tiles.bmp", TILE_SIZE, TILE_SIZE, &imageStatus);
	if(!imageStatus){
		return EFI_ABORTED;
	}
	Game->CoinSprites = loadSprites(Game->RootDirectory, L"images\\coin.bmp", TILE_SIZE, TILE_SIZE, &imageStatus);
	if(!imageStatus){
		return EFI_ABORTED;
	}
	Game->CastleSprites = loadSprites(Game->RootDirectory, L"images\\castle.bmp", TILE_SIZE, TILE_SIZE, &imageStatus);
	if(!imageStatus){
		return EFI_ABORTED;
	}
	Game->Font = loadSprites(Game->RootDirectory, L"images\\digits.bmp", 36, 36, &imageStatus);
	if(!imageStatus){
		return EFI_ABORTED;
	}
	Game->CursorSprite = loadSprites(Game->RootDirectory, L"images\\cursor.bmp", 40, 40, &imageStatus);
	if(!imageStatus){
		return EFI_ABORTED;
	}

	//Locate EFI_SIMPLE_POINTER_PROTOCOL used to read from the mouse driver. (Running this game doesn't require a mouse driver nor an actual mouse.)
	status = gBS->LocateProtocol(
		&gEfiSimplePointerProtocolGuid,
		NULL,
		(VOID**) & Game->Mouse
	);
	if(EFI_ERROR(status)){
		Print(L"Error: Simple Pointer Protocol not found. Press any key to continue.\n");
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
		return EFI_ABORTED;
	}

	//Create timer event for timing the game execution.
	gBS->CreateEvent(
		EVT_TIMER,			//Type,
		0,					//NotifyTpl - the task priority level of event notifications,
		NULL,				//NotifyFunction, OPTIONAL
		NULL,				//*NotifyContext, OPTIONAL
		&Game->TimerEvent	//*Event
	);
	gBS->SetTimer(
		Game->TimerEvent,	//Event,
		TimerPeriodic,		//Type,
		1					//TriggerTime - The number of 100ns units until the timer expires. Its effect may vary when used in virtual machines or real hardware.
	);

	//This event waits for a key to be pressed.
	Game->events[0] = gST->ConIn->WaitForKey;

	//This event waits for a mouse input (Triggered only if a mouse driver exists and a mouse is connected).
	Game->events[1] = Game->Mouse->WaitForInput;

	//This event is triggered periodically.
	Game->events[2] = Game->TimerEvent;

	Game->quit = 0;
	Game->died = 0;
	Game->coinsAnimationTime = MONEY_ANIMATION_DURATION;
	Game->mouseX = 0;
	Game->mouseY = 0;
	Game->showMouseCursor = FALSE;
	Game->isMouseMoving = FALSE;

	return EFI_SUCCESS;
}
void freeAllocatedMemory(GameStruct * Game){
	FreePool(Game->Blocks);
	for(UINT16 i = 0; i < 12; i++){
		FreePool(Game->PlayerSprites->sprites[i]);
	}
	FreePool(Game->PlayerSprites);
	for(UINT16 i = 0; i < 5; i++){
		FreePool(Game->BlocksSprites->sprites[i]);
	}
	FreePool(Game->BlocksSprites);
	for(UINT16 i = 0; i < 8; i++){
		FreePool(Game->CoinSprites->sprites[i]);
	}
	FreePool(Game->CoinSprites);
	for(UINT16 i = 0; i < 16; i++){
		FreePool(Game->CastleSprites->sprites[i]);
	}
	FreePool(Game->CastleSprites);
	for(UINT16 i = 0; i < 10; i++){
		FreePool(Game->Font->sprites[i]);
	}
	FreePool(Game->Font);
	Game->RootDirectory->Close(Game->RootDirectory);
}

typedef struct{
	unsigned width; //Level width in pixels. Limits the camera movement.
	unsigned height; //Level height in pixels. The player is killed when they fall out of the map (when this height is crossed). Level height limits the camera movement.
	unsigned blockCount;
	vec2i castlePos;
} LevelStruct;
//Highly modified version of @rubikshift 's "InitLevel" function.
EFI_STATUS loadLevel(LevelStruct * Level, PlayerStruct * Player, GameStruct * Game, CHAR16 * levelName){
	setupPlayer(Player, rvec2i(0, 0), 0);
	Level->castlePos = rvec2i(600, 600);
	
	EFI_FILE_PROTOCOL* LevelFile;
	EFI_STATUS fileStatus = Game->RootDirectory->Open(Game->RootDirectory, &LevelFile, levelName, EFI_FILE_MODE_READ, 0);
	if(EFI_ERROR(fileStatus)){
		return EFI_ABORTED;
	}

	unsigned width, height;
	UINTN bufferSize = sizeof(unsigned);
	//Read from the file the width and height (in the number of blocks) of the current map.
	LevelFile->Read(LevelFile, &bufferSize, (VOID*) &height);
	LevelFile->Read(LevelFile, &bufferSize, (VOID*) &width);

	//Read the max number of blocks used in the current map from the file.
	LevelFile->Read(LevelFile, &bufferSize, (VOID*) &Level->blockCount);
	
	bufferSize = width * height * sizeof(CHAR8);
	CHAR8* tileBuffer = AllocatePool(bufferSize);

	//Read the map layout from the file to a buffer. Each character read from the file corresponds to the type of a different object that exists on the map.
	LevelFile->Read(LevelFile, &bufferSize, (VOID*) tileBuffer);
	//Allocate memory for all blocks that will create the terrain of the map. This includes coins.
	Game->Blocks = AllocatePool(sizeof(ObjectStruct) * Level->blockCount);

	unsigned x, y, objIdx = 0;
	for(unsigned bufferIdx = 0; bufferIdx < bufferSize; bufferIdx++){
		x = bufferIdx % width;
		y = bufferIdx / width;
		if(tileBuffer[bufferIdx] == 'G'){ //Green brick
			setupObject(&Game->Blocks[objIdx], rvec2i(x * TILE_SIZE, y * TILE_SIZE), green_brick, 0, TRUE, TRUE);
			objIdx++;
		}
		else if(tileBuffer[bufferIdx] == 'R'){	//Red brick
			setupObject(&Game->Blocks[objIdx], rvec2i(x * TILE_SIZE, y * TILE_SIZE), red_brick, 1, TRUE, TRUE);
			objIdx++;
		}
		else if(tileBuffer[bufferIdx] == 'M'){ //Mossy red brick
			setupObject(&Game->Blocks[objIdx], rvec2i(x * TILE_SIZE, y * TILE_SIZE), mossy_brick, 2, TRUE, TRUE);
			objIdx++;
		}
		else if(tileBuffer[bufferIdx] == 'W'){ //Web - a trap that kills the player 
			setupObject(&Game->Blocks[objIdx], rvec2i(x * TILE_SIZE, y * TILE_SIZE), web, 3, TRUE, TRUE);
			objIdx++;
		}
		else if(tileBuffer[bufferIdx] == 'S'){ //Web with a spider - a trap that kills the player 
			setupObject(&Game->Blocks[objIdx], rvec2i(x * TILE_SIZE, y * TILE_SIZE), spider, 4, TRUE, TRUE);
			objIdx++;
		}
		else if(tileBuffer[bufferIdx] == 'C'){ //Coin - an animated collectable
			setupObject(&Game->Blocks[objIdx], rvec2i(x * TILE_SIZE, y * TILE_SIZE), coin, 0, TRUE, FALSE);
			objIdx++;
		}
		else if (tileBuffer[bufferIdx] == 'P'){ //Player spawn point
			Player->Base.pos = rvec2i(x * TILE_SIZE, y * TILE_SIZE);
		}
		else if (tileBuffer[bufferIdx] == 'E'){	//Castle location - the end goal of the game
			Level->castlePos = rvec2i(x * TILE_SIZE, y * TILE_SIZE);
		}
	}

	Level->width = width * TILE_SIZE;
	Level->height = height * TILE_SIZE;

	LevelFile->Close(LevelFile);
	FreePool(tileBuffer);
	return EFI_SUCCESS;
}

typedef struct{
	vec2i pos;
} CameraStruct;

void drawBitmap(EFI_GRAPHICS_OUTPUT_PROTOCOL* Screen, EFI_GRAPHICS_OUTPUT_BLT_PIXEL* Bitmap, UINTN destX, UINTN destY, UINTN width, UINTN height){
	//"The basic graphics operation in the EFI_GRAPHICS_OUTPUT_PROTOCOL is the Block Transfer or Blt. The Blt
	//operation allows data to be read or written to the video adapter’s video memory." ~ UEFI Spec. 2.10., page 426.

	//"Blt a rectangle of pixels on the graphics screen." ~ UEFI Spec. 2.10., page 432.
	Screen->Blt(
		Screen,											//*This - EFI_GRAPHICS_OUTPUT_PROTOCOL,
		Bitmap,											//*BltBuffer, OPTIONAL
		EfiBltBufferToVideo,							//BltOperation,
		0, 0,											//SourceX & SourceY,
		destX, destY,									//DestinationX & DestinationY,
		width, height,									//Width & Height,
		width * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) 	//Delta, OPTIONAL - Length in bytes of a row in the bitmap. Required if only a part of the bitmap is drawn.
	);
}
void drawGameObject(ObjectStruct * Object, EFI_GRAPHICS_OUTPUT_PROTOCOL* Screen, SpriteArray * Bitmap, CameraStruct * Camera){
	//Don't draw objects outside the camera.
	if(Object->type != player && (!Object->isActive || Object->pos.x > Camera->pos.x + SCREEN_WIDTH - TILE_SIZE
		|| Object->pos.y > Camera->pos.y + SCREEN_HEIGHT - TILE_SIZE
		|| Object->pos.x < Camera->pos.x || Object->pos.y < Camera->pos.y
	)){
		return;
	}
	drawBitmap(Screen, Bitmap->sprites[Object->frameIdx], Object->pos.x - Camera->pos.x, Object->pos.y - Camera->pos.y, TILE_SIZE, TILE_SIZE);
}

void useKeyboardInput(PlayerStruct * Player, GameStruct * Game, CameraStruct * Camera){
	EFI_INPUT_KEY key;
	
	//Read a pressed key from the keyboard driver.
	gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
		
	switch(key.ScanCode){
		case SCAN_ESC: //Exit the game
			Game->quit = 1;
			break;
		case SCAN_UP: //Jump
			if(Player->canJump && !Player->isFalling){
				Player->playerJumpTime = PLAYER_JUMP_DURATION;
				Player->isJumping = TRUE;
				Player->canJump = FALSE;
				if(Player->direction){
					Player->Base.frameIdx = 8;
				}
				else{
					Player->Base.frameIdx = 9;
				}
			}
			break;
		case SCAN_LEFT: //Move player to the left and animate the movement
			Player->momentum.x = -PLAYER_SPEED;
			Player->direction = FALSE;
			if(Player->animationTime <= 0){
				Player->Base.frameIdx = (Player->Base.frameIdx + 1) % 4 + 4;
				Player->animationTime = ANIMATION_DURATION;
			}
			else{
				Player->animationTime--;
			}
			if(Player->isJumping){
				Player->Base.frameIdx = 9;
			}
			if(Player->isFalling){
				Player->Base.frameIdx = 11;
			}
			break;
		case SCAN_RIGHT: //Move player to the right and animate the movement
			Player->momentum.x = PLAYER_SPEED;
			Player->direction = TRUE;
			if(Player->animationTime <= 0){
				Player->Base.frameIdx = (Player->Base.frameIdx + 1) % 4; 
				Player->animationTime = ANIMATION_DURATION;
			}
			else{
				Player->animationTime--;
			}
			if(Player->isJumping){
				Player->Base.frameIdx = 8;
			}
			if(Player->isFalling){
				Player->Base.frameIdx = 10;
			}
			break;
		case SCAN_F1: //Show mouse cursor
			Game->showMouseCursor = !Game->showMouseCursor;
			break;
		case SCAN_F2: //Teleport player to the cursor position relative to the camera
			if(Game->showMouseCursor){
				Player->Base.pos.x = Game->mouseX + Camera->pos.x;
				Player->Base.pos.y = Game->mouseY + Camera->pos.y;
			}
			break;
		case SCAN_F5: //Move mouse cursor to the left
			Game->mouseX -= 10;
			Game->isMouseMoving = TRUE;
			break;
		case SCAN_F6: //Move mouse cursor up
			Game->mouseY -= 10;
			Game->isMouseMoving = TRUE;
			break;
		case SCAN_F7: //Move mouse cursor down
			Game->mouseY += 10;
			Game->isMouseMoving = TRUE;
			break;
		case SCAN_F8: //Move mouse cursor to the right
			Game->mouseX += 10;
			Game->isMouseMoving = TRUE;
			break;
		case SCAN_NULL:
			break;
		default: break;
	}
}

//This function is used only if a mouse driver is loaded to the memory and a mouse is connected.
void useMouseInput(PlayerStruct * Player, GameStruct * Game, CameraStruct * Camera){
	EFI_SIMPLE_POINTER_STATE MouseState;
	//Get the current state of the mouse (its movement and pressed buttons). 
	Game->Mouse->GetState(Game->Mouse, &MouseState);

	//Move the mouse.
	Game->mouseX += MouseState.RelativeMovementX;
	Game->mouseY += MouseState.RelativeMovementY;
	
	if(MouseState.LeftButton){ //If the left mouse button is pressed, trigger a jump
		if(!Player->isJumping && !Player->isFalling){
			Player->playerJumpTime = PLAYER_JUMP_DURATION;
			Player->isJumping = TRUE;
			if(Player->direction){
				Player->Base.frameIdx = 8;
			}
			else{
				Player->Base.frameIdx = 9;
			}
		}
	}
	else if(MouseState.RightButton && Game->showMouseCursor){ //If the right mouse button is pressed teleport player to the mouse cursor.
		Player->Base.pos.x = Game->mouseX + Camera->pos.x;
		Player->Base.pos.y = Game->mouseY + Camera->pos.y;
		if(Player->Base.pos.x < 0){
			Player->Base.pos.x = 0;
		}
		if(Player->Base.pos.y < 0){
			Player->Base.pos.y = 0;
		}
	}
}

//This function simulates falling for the player.
void useGravity(PlayerStruct * Player){
	Player->momentum.y += FALL_SPEED;
	if(Player->momentum.y > 2){
		Player->momentum.y = 2;
	}
	
	if(Player->isJumping){
		Player->momentum.y = -JUMP_SPEED;
		Player->playerJumpTime--;
		if(Player->playerJumpTime <= 0){
			Player->playerJumpTime = PLAYER_JUMP_DURATION;
			Player->isJumping = FALSE;
			Player->isFalling = TRUE;
		}
	}
}

BOOLEAN areObjectsOverlaping(vec2i pos1, vec2i size1, vec2i pos2, vec2i size2){
    vec2i pos1s = {pos1.x + size1.x, pos1.y + size1.y};
    vec2i pos2s = {pos2.x + size2.x, pos2.y + size2.y};
    BOOLEAN canOverlap = FALSE;

	//Check if two objects collide on the x axis.
    if((pos1.x >= pos2.x && pos1.x <= pos2s.x) || (pos1s.x >= pos2.x && pos1s.x <= pos2s.x))
        canOverlap = TRUE;
    if((pos2.x >= pos1.x && pos2.x <= pos1s.x) || (pos2s.x >= pos1.x && pos2s.x <= pos1s.x))
        canOverlap = TRUE;

    if(!canOverlap)
        return FALSE;

	//Check if two objects collide on the y axis.
    if((pos1.y >= pos2.y && pos1.y <= pos2s.y) || (pos1s.y >= pos2.y && pos1s.y <= pos2s.y))
        return TRUE;
    if((pos2.y >= pos1.y && pos2.y <= pos1s.y) || (pos2s.y >= pos1.y && pos2s.y <= pos1s.y))
        return TRUE;

    return FALSE;
}
vec2i countMinimalDistanceBetween(vec2i pos1, vec2i size1, vec2i pos2, vec2i size2, double precision){
    vec2i distance = {0.0, 0.0};
	vec2i sPos2 = {pos1.x + size1.x, pos1.y + size1.y};
	vec2i mPos2 = {pos2.x + size2.x, pos2.y + size2.y};
    if(mPos2.x < pos1.x){
        distance.x = pos1.x-precision-size2.x-pos2.x;
    }
    else if(pos2.x > sPos2.x){
        distance.x = sPos2.x+precision-pos2.x;
    }
    if(mPos2.y < pos1.y){
        distance.y = pos1.y-precision-size2.y-pos2.y;
    }
    else if(pos2.y > sPos2.y){
        distance.y = sPos2.y+precision-pos2.y;
    }
    return distance;
}
int abs(int number){
	if(number < 0){
		return -number;
	}
	return number;
}
void checkCollisions(unsigned blockCount, GameStruct * Game, PlayerStruct * Player){
	vec2i tileSize = {TILE_SIZE, TILE_SIZE};

	for(unsigned i = 0; i < blockCount; i++){
		if(!Game->Blocks[i].isActive){ //If the object is disabled it will not collide with the player
			continue;
		}

		vec2i sPos = {Game->Blocks[i].pos.x, Game->Blocks[i].pos.y}; 	//Solid object position
		vec2i mPos = {Player->Base.pos.x, Player->Base.pos.y};			//Moving player position
		vec2i mPos2 = {0, 0};											//New position of the moving player

		if(Game->Blocks[i].type == coin){ //If the player collides with a coin, the coin is disabled and player gets 1 point. 
			mPos2 = rvec2i(mPos.x + Player->momentum.x, mPos.y + Player->momentum.y);
			if(areObjectsOverlaping(sPos, tileSize, rvec2i(mPos2.x, mPos2.y), tileSize)){
				Game->Blocks[i].isActive = FALSE;
				Player->coins++;
			}
			continue;
		}
		if(Game->Blocks[i].type == web || Game->Blocks[i].type == spider){ //If the player collides with a web or spider, player dies. 
			mPos2 = rvec2i(mPos.x + Player->momentum.x, mPos.y + Player->momentum.y);
			if(areObjectsOverlaping(rvec2i(sPos.x + 3, sPos.y + 3), rvec2i(tileSize.x - 6, tileSize.y - 6), rvec2i(mPos2.x, mPos2.y), tileSize)){
				Game->died = 1;
				break;
			}
			continue;
		}

		//Detect collisions with the solid blocks.

		//Find the smallest momentum needed to reach collision (without it, player would ignore smaller collisions with enough speed).
		vec2i minMomentum = countMinimalDistanceBetween(sPos, tileSize, mPos, tileSize, 0);

		//Find a new postition for the moving player.
		mPos2 = rvec2i(mPos.x + Player->momentum.x, mPos.y + Player->momentum.y);
		if(Player->momentum.x * minMomentum.x > 0 && abs(Player->momentum.x) > abs(minMomentum.x)){
			mPos2.x = mPos.x + minMomentum.x;
		}
		if(Player->momentum.y * minMomentum.y > 0 && abs(Player->momentum.y) > abs(minMomentum.y)){
			mPos2.y = mPos.y + minMomentum.y;
		}

		//Check if x axis momentum will cause a collision
		if(areObjectsOverlaping(sPos, tileSize, rvec2i(mPos2.x, mPos.y + 1), rvec2i(tileSize.x, tileSize.y - 2))){
			Player->momentum.x = countMinimalDistanceBetween(sPos, tileSize, mPos, tileSize, 0).x;
			mPos.x = Player->momentum.x;
		}

		//Check if y axis momentum will cause a collision.
		if(areObjectsOverlaping(sPos, tileSize, rvec2i(mPos.x + 1, mPos2.y), rvec2i(tileSize.x - 2, tileSize.y))){
			if(Player->isJumping){ //Stop the jump if the player hits a ceiling.
				Player->isJumping = FALSE;
				Player->isFalling = TRUE;
				if(Player->direction){
					Player->Base.frameIdx = 10;
				}
				else{
					Player->Base.frameIdx = 11;
				}
			}
			else if(Player->momentum.y > 0){ //Stop the fall if the player hits a ground.
				Player->isFalling = FALSE;
				Player->canJump = TRUE;
				if(Player->momentum.x == 0){
					if(Player->direction){
						Player->Base.frameIdx = 2;
					}
					else{
						Player->Base.frameIdx = 6;
					}
				}
			}
			Player->momentum.y = countMinimalDistanceBetween(sPos, tileSize, mPos, tileSize, 0).y;
		}
	}
}

void movePlayer(PlayerStruct * Player){
	if(Player->momentum.x == 0 && Player->momentum.y == 0){
		Player->isMoving = FALSE;
		return;
	}
	Player->isMoving = TRUE;

	Player->Base.pos.x += Player->momentum.x;
	Player->Base.pos.y += Player->momentum.y;
	
	//Don't let the player jump out of the level on y axis. Currently this event doesn't trigger the collision.
	if(Player->Base.pos.y < 0){
		Player->Base.pos.y = 0;
	}
	
	//Slow down the player.
	if(Player->momentum.x > 0){
		Player->momentum.x--;
	}
	else if(Player->momentum.x < 0){
		Player->momentum.x++;
	}
}

void animateCoins(GameStruct * Game, unsigned blockCount){
	if(Game->coinsAnimationTime > 0){
		Game->coinsAnimationTime--;
		return;
	}
	Game->coinsAnimationTime = MONEY_ANIMATION_DURATION;

	for(unsigned blockIdx = 0; blockIdx < blockCount; blockIdx++){
		if(Game->Blocks[blockIdx].type == coin){
			Game->Blocks[blockIdx].frameIdx = (Game->Blocks[blockIdx].frameIdx + 1) % 8; 
		}
	}
}

//Camera follows the player, but is limited by the level borders.
void moveCamera(CameraStruct * Camera, ObjectStruct * Player, unsigned levelWidth, unsigned levelHeight){
	Camera->pos = rvec2i(Player->pos.x - (SCREEN_WIDTH / 2), Player->pos.y - (SCREEN_HEIGHT / 2));
	if(Camera->pos.x < 0){
		Camera->pos.x = 0;
	}
	if(Camera->pos.x + SCREEN_WIDTH > levelWidth){
		Camera->pos.x = levelWidth - SCREEN_WIDTH;
	}
	if(Camera->pos.y < 0){
		Camera->pos.y = 0;
	}
	if(Camera->pos.y + SCREEN_HEIGHT > levelHeight){
		Camera->pos.y = levelHeight - SCREEN_HEIGHT;
	}
}

void drawEverything(GameStruct * Game, PlayerStruct * Player, vec2i castlePos, CameraStruct * Camera, unsigned blockCount){
	if(Player->isMoving || Game->isMouseMoving){
		clearScreenWithColor(Game->Screen, 119, 181, 254);
	}
	Game->isMouseMoving = FALSE;
	
	//Draw blocks and coins 
	for(unsigned i = 0; i < blockCount; i++){
		if(Game->Blocks[i].type == coin){
			drawGameObject(&Game->Blocks[i], Game->Screen, Game->CoinSprites, Camera);
		}
		else{
			drawGameObject(&Game->Blocks[i], Game->Screen, Game->BlocksSprites, Camera);
		}
	}

	//Draw player
	drawGameObject(&Player->Base, Game->Screen, Game->PlayerSprites, Camera);
	
	//Divide player coins count into digits and draw them with the bitmap "font" (this "font" has only digits).
	unsigned digit0 = Player->coins;
	if(Player->coins > 9){
		digit0 -= (int)(Player->coins / 10) * 10;
	}
	unsigned digit1 = (int)(Player->coins / 10);
	if(Player->coins > 99){
		digit1 -= (int)(Player->coins / 100) * 100;
	}
	drawBitmap(Game->Screen, Game->Font->sprites[digit0], 46, 10, 36, 36);
	drawBitmap(Game->Screen, Game->Font->sprites[digit1], 10, 10, 36, 36);

	//Draw castle (the end goal of the game).
	for(int i = 0; i < 16; i++){
		//Don't draw the parts of the castle that are not visible by the player.
		if(castlePos.x + (i % 4) * 40 > Camera->pos.x + SCREEN_WIDTH - 40
			|| castlePos.y + (i / 4) * 40 > Camera->pos.y + SCREEN_HEIGHT - 40
			|| castlePos.x + (i % 4) * 40 < Camera->pos.x
			|| castlePos.y + (i / 4) * 40 < Camera->pos.y
		){
			continue;
		}
		drawBitmap(Game->Screen, Game->CastleSprites->sprites[i],
			castlePos.x + (i % 4) * 40 - Camera->pos.x,
			castlePos.y + (i / 4) * 40 - Camera->pos.y,
			40, 40
		);
	}

	//Draw the mouse cursor.
	if(Game->showMouseCursor){
		drawBitmap(Game->Screen, Game->CursorSprite->sprites[0], Game->mouseX, Game->mouseY, 40, 40);
	}
}

void checkGameState(GameStruct * Game, PlayerStruct * Player, LevelStruct * Level){
	UINTN eventId;
	
	//DEATH
	if(Game->died || Player->Base.pos.y > Level->height){
		clearScreenWithColor(Game->Screen, 0, 0, 0);
		Print(L"You died. F.\n");
		Game->quit = 1;
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
	}

	//WIN
	if(areObjectsOverlaping(rvec2i(Level->castlePos.x, Level->castlePos.y), rvec2i(160, 160), Player->Base.pos, rvec2i(TILE_SIZE, TILE_SIZE))){
		clearScreenWithColor(Game->Screen, 0, 0, 0);
		Print(L"You win! Your score: %d.\n", Player->coins);
		Game->quit = 1;
		gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
	}
}

EFI_STATUS EFIAPI UefiMain (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE * SystemTable){
	GameStruct Game;
	if(setupGame(&Game) == EFI_ABORTED){
		return EFI_ABORTED;
	}
	
	LevelStruct Level;
	PlayerStruct Player;
	if(loadLevel(&Level, &Player, &Game, L"levels\\level.bin") == EFI_ABORTED){
		return EFI_ABORTED;
	}
	
	CameraStruct Camera = {rvec2i(0, 0)};

	UINTN eventId;

	clearScreenWithColor(Game.Screen, 119, 181, 254);

	//GAME LOOP
	while(!Game.quit){
		gBS->WaitForEvent(3, Game.events, &eventId);
		
		if(eventId == 0){ //Check if keyboard event is triggered.
			useKeyboardInput(&Player, &Game, &Camera);
		}
		else if(eventId == 1){ //Check if mouse event is triggered.
			useMouseInput(&Player, &Game, &Camera);
		}
		else if(eventId == 2){ //Check if timer is triggered.
			useGravity(&Player);

			checkCollisions(Level.blockCount, &Game, &Player);

    		movePlayer(&Player);

			animateCoins(&Game, Level.blockCount);

			moveCamera(&Camera, &Player.Base, Level.width, Level.height);

			drawEverything(&Game, &Player, Level.castlePos, &Camera, Level.blockCount);

			checkGameState(&Game, &Player, &Level);
		}
	}

	gST->ConIn->Reset(gST->ConIn, 0);
	freeAllocatedMemory(&Game);

	Print(L"Press any key to exit.\n");
	gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventId);
	gST->ConIn->Reset(gST->ConIn, 0);

	return EFI_SUCCESS;
}