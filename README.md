# UEFI_Platformer
Platformer game written for UEFI Shell.

Project inspired by https://github.com/rubikshift/UEFI_MARIO and written in C with the use of UEFI libraries.

Project uses Edk2 for building the game.
Game was tested in Qemu with Ovmf.

The game can be run on real hardware (in UEFI shell), but due to not consistent performance and flickering it is recommended to run it in a virtual machine.

## Edk2



## Qemu

## Running on real hardware

In order to run this game on real hardware you need to create a bootable pendrive with uefi shell, game binary and other game assets.

## UEFI functionalities

Implemented in the game:
- Drawing on the screen with **Blt** function from "Protocol/GraphicsOutput.h".  (UEFI Spec. 2.10., page 426.)
- Keyboard input.
- Reading from files from "Protocol/SimpleFileSystem.h".
- Mouse input from "Protocol/SimplePointer.h".
- Timer.

Failed to implement in the game:
- Keyboard input with non-blocking keys - currently you can only press one key at a time.
- Mouse input in a virtual machine - Ovmf doesn't support the mouse input and adding mouse drivers from edk2 project doesn't help.

## Binaries

In "binaries" folder there are few versions of the same binary file with the game, but with different screen resolutions (mentioned in the file names).

## Game assets

In order to run, game needs two folders with assets:
- images - with files inside:
  - player.bmp - player animation,
  - tiles.bmp - solid objects creating the terrain,
  - coins.bmp - coin animation,
  - castle.bmp - tiles needed to draw the whole castle sprite (game end goal),
  - digits.bmp - bitmap font for digits - used for displaying the current score,
  - cursor.bmp - mouse cursor,
- levels - with a file:
  - level.bin - information how to build the current game level.

Uefi accepts only .bmp images and binary files.

## Building levels

Game is using levelMaker.py script. It's a slightly modified version of https://github.com/rubikshift/UEFI_MARIO/blob/master/levelmaker.py

This python script converts a text file into a binary file. The text file consists of letters that correspond to game objects:
- **G** - green brick,
- **R** - red brick,
- **M** - mossy brick,
- **W** - web,
- **S** - web with a spider,
- **C** - coin,
- **P** - player spawn point,
- **E** - castle location - the end goal of the game.

Use dots or other ascii characters to fill the empty space between the game objects.

Use syntax:
    
    python levelMaker.py level.txt level.bin

Currently, the game can only load a map with a name: "level.bin".

## Game controls

- **ESC** - exit the game

- **Up Arrow** - jump

- **Left Arrow** - move to the left

- **Right Arrow** - move to the right

- **F1** - enable or disable mouse cursor (cursor is turn off by default)

- **F2** - teleport player to the mouse cursor

- **F5** - move mouse cursor to the left

- **F6** - move mouse cursor up

- **F7** - move mouse cursor down

- **F8** - move mouse cursor to the right

- **Left Mouse Button** - jump (not blocking alternative)

- **Right Mouse Button** - teleport player to the mouse cursor (alternative)

## Game objectives

The goal of the game is to reach the castle on the end of a loaded level.

While playing, player can collect coins that turn into a score displayed in the top left corner of the screen. If the player wins, score is also displayed on the end screen.

## Game screenshots

<img src="screenshots/1.png" width="500"/>


<img src="screenshots/2.png" width="500"/>


<img src="screenshots/3.png" width="500"/>


<img src="screenshots/4.png" width="500"/>


<img src="screenshots/5.png" width="500"/>


<img src="screenshots/6.png" width="500"/>


<img src="screenshots/win.png" width="500"/>


<img src="screenshots/lose.png" width="500"/>