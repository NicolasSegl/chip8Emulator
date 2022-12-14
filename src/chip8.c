#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "chip8.h"

// constants
const DoubleByte PROGRAM_MEMORY_ADDRESS = 0x200;   // chip8's programs start at an offset of 0x200 
const DoubleByte MEMORY_SIZE            = 4096;    // chip8 has 4kb of memory
const DoubleByte NUM_OF_PIXELS          = 64 * 32; // chip8 supports 64 * 32 = 2048 pixels in its graphics array

const Byte NUM_OF_REGISTERS    = 15; // there are 15 general purpose registers
const Byte NUM_OF_STACK_LEVELS = 16; // 16 levels of stack
const Byte NUM_OF_KEYS         = 16; // 16 keys in chip8's hex based keypad
const Byte SCREEN_WIDTH        = 64; // chip8 has graphics that support 64 pixels in width
const Byte SCREEN_HEIGHT       = 32; // chip8 has graphics that support 32 pixels in height

#define FONTSET_SIZE 0x50

/*
    the following array represents chip8's "fontset"
    this is a construct that allows for the software to draw characters to the screen via
    drawing the pixels for numbers 0-9 and letters A-F
    each character or number is 4 pixels wide and 5 pixels high
*/
const Byte fontset[FONTSET_SIZE] =
{ 
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// intiailizes all of the things chip8 needs to properly operate
void initChip8(chip8* chip8) 
{ 
    chip8->programCounter = PROGRAM_MEMORY_ADDRESS; // set the program counter to where the program's memory starts
    chip8->opcode         = 0;                      // reset the opcode
    chip8->indexRegister  = 0;                      // reset the index register
    chip8->stackPointer   = 0;                      // reset the stack pointer
    chip8->drawFlag       = false;                  // reset the draw flag

    // clear all the pixels on the display
    for (int pixel = 0; pixel < NUM_OF_PIXELS; pixel++)
    {
        chip8->pixels[pixel] = 0;
    }

    // clear the stack
    for (int stackLevel = 0; stackLevel < NUM_OF_STACK_LEVELS; stackLevel++)
    {
        chip8->stack[stackLevel] = 0;
    }

    // clear the registers
    for (int r = 0; r < NUM_OF_REGISTERS; r++)
    {
        chip8->registers[r] = 0;
    }
    
    chip8->carryRegister = 0;

    // clear the hex-based keypad's keys (i.e. set them to false, indicating that they are not pressed)
    for (int key = 0; key < NUM_OF_KEYS; key++)
    {
        chip8->keys[key] = false;
    }

    // clear the memory
    for (int byteAddr = 0; byteAddr < MEMORY_SIZE; byteAddr++)
    {
        chip8->memory[byteAddr] = 0;
    }

    // load the fontset into memory
    for (int byteAddr = 0; byteAddr < FONTSET_SIZE; byteAddr++)
    {
        chip8->memory[byteAddr] = fontset[byteAddr];
    }

    chip8->soundTimer = 0; // reset sound timer
    chip8->delayTimer = 0; // reset delay timer

    // seed the random function from stdlib.h
    srand(time(NULL));
}

// loads a ROM file into the memory of the chip8
bool loadChip8(const char* romdir, chip8* chip8)
{
    printf("Loading %s...\n", romdir);

    // load the romdir (by reading it as binary data, hence the "rb")
    FILE* romFile = fopen(romdir, "rb");

    // if the rom file could not be opened properly, exit the function
    if (romFile == NULL)
    {
        printf("Error loading ROM file!\n");
        return false;
    }

    // find the size of the file so that we can read in the proper number of bytes
    // seek to the end of the file
    fseek(romFile, 0L, SEEK_END);

    // ask for the position when at the end of the file
    int romSize = ftell(romFile);

    // seek back to the beginning of the file
    rewind(romFile);

    // see if the size of the ROM is too big to fit into chip8's memory (of 4k)
    if (4096 - 0x200 < romSize) 
    {
        printf("ROM is too big to load into chip8's 4k memory");
        return false;
    }

    // read in the data in the rom into a buffer (which will later by stored into our memory buffer)
    Byte* romBuffer = (Byte*)malloc(sizeof(Byte) * romSize);
    if (romBuffer == NULL)
    {
        printf("Error allocating the memory for the ROM buffer");
        return false;
    }

    // read the ROM's data into the rom buffer
    fread(romBuffer, sizeof(Byte), romSize, romFile);

    // set the data in the chip8's memory
    for (int addr = 0x200; addr < 0x200 + romSize; addr++)
    {
        // the memory for the program starts at 0x200
        chip8->memory[addr] = romBuffer[addr - 0x200];
    }
    
    // cleanup 
    fclose(romFile);
    free(romBuffer);

    printf("Successfully loaded ROM!\n");
    return true;
}

// emulates a single cpu cycle
void emulateChip8Cycle(chip8* chip8)
{
    /*
        fetch the opcode using the current address that the program counter is pointing at. because the
        opcodes are formatted according to the little endian standard, we need to shift the first byte 
        that the program counter is looking at one byte to the left, and then read in the next byte as well
    */
    chip8->opcode = (chip8->memory[chip8->programCounter] << 8) | chip8->memory[chip8->programCounter + 1];

    /* 
        decoding the opcode:
        the first nibble (4 bits) of the opcode will tell us which instruction is being executed
        we can use the following switch statement to mask which instruction is actually being called 
        (by masking the 4 leftmost bits of the opcode
    */
    switch (chip8->opcode & 0xF000) // 0xF000 = 0b1111 0000 0000 0000, which masks out all but the leftmost 4 bits of the opcode
    {
        case 0x0000: // either opcode 00E0 or 00EE
        {
            switch (chip8->opcode & 0x0FFF)
            {
                case 0x00E0: // opcode 00E0: clear the screen
                {
                    // reset all the pixels 
                    for (int pixel = 0; pixel < NUM_OF_PIXELS; pixel++)
                    {
                        chip8->pixels[pixel] = 0;
                    }

                    chip8->programCounter += 2; 
                    chip8->drawFlag = true; // set the draw flag to 1 (indicating that we need to update the screen)
                    
                    break;
                }

                case 0x00EE: // opcode 00EE: return from subroutine, meaning that we need to return the program counter to the address stored on the stack
                {
                    // decrement the stack pointer
                    chip8->stackPointer--;

                    // set the program counter to the address stored on the stack
                    chip8->programCounter = chip8->stack[chip8->stackPointer];
                    chip8->programCounter += 2;

                    break;
                }

                default:
                {
                    printf("Unknown opcode %.4X\n", chip8->opcode);
                    exit(5);
                }
            }

            break;
        }

        case 0x1000: // opcode 1NNN: jump to address NNN
        {
            chip8->programCounter = chip8->opcode & 0x0FFF;

            break;
        }

        case 0x2000: // opcode 2NNN: call subroutine at address NNN
        {
            // store the current memory address into the stack at its current level, then increment the stack pointer
            chip8->stack[chip8->stackPointer] = chip8->programCounter;

            // increment the stack pointer
            chip8->stackPointer++;

            // go to the code at the subroutine
            chip8->programCounter = chip8->opcode & 0x0FFF;
            
            break;
        }
        
        case 0x3000: // opcode 3XNN: compares register[x] to NN, and skips the next instruction if they are equal
        {
            // opcode & 0x0F00 >> 8 masks out the x (so the index of the register we're after) 
            // and then shifts it 8 to the right (as to get the actual index and prevent overflow)
            if ((chip8->registers[(chip8->opcode & 0x0F00) >> 8]) == (chip8->opcode & 0x00FF))
            {
                chip8->programCounter += 4;
                break;
            }

            chip8->programCounter += 2;

            break;
        }

        case 0x4000: // opcode 4XNN: compares registers[x] to NN, seeing if registers[x] is not equal to NN. skips the next instruction if it passes 
        {
            if ((chip8->registers[(chip8->opcode & 0x0F00) >> 8]) != (chip8->opcode & 0x00FF))
            {
                chip8->programCounter += 4;
                break;
            }

            chip8->programCounter += 2;

            break;
        }

        case 0x5000: // opcode 5XY0: compares registers[x] to registers[y], skipping the next instruction if they are equal
        {
            // unlike in previous opcode decodings, we shift the index for registers[y] by 4 because each hexadecimal value is equal
            // to 4 bits, and so we have to rightshift by 4 bits in order to get the index as a 4 bit number (instead of a 8 bit number, which would cause overflow)
            if (chip8->registers[(chip8->opcode & 0x0F00) >> 8] == chip8->registers[(chip8->opcode & 0x00F0) >> 4])
            {
                chip8->programCounter += 4;
                break;
            }
            
            chip8->programCounter += 2;

            break;
        }

        case 0x6000: // opcode 6XNN: sets registers[x] to NN
        {
            chip8->registers[(chip8->opcode & 0x0F00) >> 8] = chip8->opcode & 0x00FF;
            chip8->programCounter += 2;

            break;
        }

        case 0x7000: // opcode 7XNN: adds registers[x] and NN (storing the result in registers[x])
        {
            chip8->registers[(chip8->opcode & 0x0F00) >> 8] += chip8->opcode & 0x00FF;
            chip8->programCounter += 2;

            break;
        }

        case 0x8000: // for opcodes 8XY(0-9)
        {
            switch (chip8->opcode & 0x000F)
            {
                case 0x0: // opcode 8XY0: sets registers[x] to value of registers[y]
                {
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] = chip8->registers[(chip8->opcode & 0x00F0) >> 4];
                    chip8->programCounter += 2;
                    break;
                }

                case 0x1: // opcode 8XY1: sets registers[x] to registers[x] | registers[y]
                {
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] |= chip8->registers[(chip8->opcode & 0x00F0) >> 4];
                    chip8->programCounter += 2;
                    break;
                }

                case 0x2: // opcode 8XY2: sets registers[x] to registers[x] & registers[y]
                {
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] &= chip8->registers[(chip8->opcode & 0x00F0) >> 4];
                    chip8->programCounter += 2;
                    break;
                }

                case 0x3: // opcode 8XY3: sets registers[x] to registers[x] ^ registers[y]
                {
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] ^= chip8->registers[(chip8->opcode & 0x00F0) >> 4];
                    chip8->programCounter += 2;
                    break;
                }

                case 0x4: // opcode 8XY4: sets registers[x] to registers[x] + registers[y], and sets the carry register to 1 when there's a carry (indicating an overflow), or 0 otherwise
                {
                    chip8->carryRegister = 0;
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] += chip8->registers[(chip8->opcode & 0x00F0) >> 4];

                    // if registers[y] is greater than [255 (max value a register can store) minus registers[x]], then set the carry register to 1 (to indicate an overflow)
                    if (chip8->registers[(chip8->opcode & 0x00F0) >> 4] > 0xFF - chip8->registers[(chip8->opcode & 0x0F00) >> 8])
                    {
                        chip8->carryRegister = 1;
                    }

                    chip8->programCounter += 2;
                    break;
                }

                case 0x5: // opcode 8XY5: sets registers[x] to registers[x] - registers[y], and sets the carry register to 0 when there's a borrow (indicating an overflow), or 1 otherwise
                {
                    chip8->carryRegister = 1;

                    // if registers[y] is greater than registers[x], then set the carry register to 0 (to indicate an overflow)
                    if (chip8->registers[(chip8->opcode & 0x00F0) >> 4] > chip8->registers[(chip8->opcode & 0x0F00) >> 8])
                    {
                        chip8->carryRegister = 0;
                    }

                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] -= chip8->registers[(chip8->opcode & 0x00F0) >> 4];

                    chip8->programCounter += 2;
                    break;
                }

                case 0x6: // opcode 8XY6: sets registers[x] to registers[x] >> 1, and sets the carry bit to the least significant bit of registers[x]
                {
                    // get the least significant digit of registers[x]
                    chip8->carryRegister = chip8->registers[(chip8->opcode & 0x0F00) >> 8] & 1;
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] >>= 1;
                    
                    chip8->programCounter += 2;
                    break;
                }

                case 0x7: // opcode: 8XY7: sets registers[x] to registers[y] - registers[x], and sets the carry bit to 0 if there is a borrow, and 1 otherwise
                {
                    chip8->carryRegister = 1;

                    // if registers[x] is greather than registers[y], then set the carry bit to 0 (to indicate an overflow)
                    if (chip8->registers[(chip8->opcode & 0x0F00) >> 8] > chip8->registers[(chip8->opcode & 0x00F0) >> 4])
                    {
                        chip8->carryRegister = 0;
                    }

                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] = chip8->registers[(chip8->opcode & 0x00F0) >> 4] - chip8->registers[(chip8->opcode & 0x0F00) >> 8];

                    chip8->programCounter += 2;
                    break;
                }

                case 0xE: // opcode 8XYE: sets registers[x] to registers[x] << 1, and sets the carry bit to the most significant bit of registers[x]
                {
                    // get the most significant digit of registers[x]
                    chip8->carryRegister = chip8->registers[(chip8->opcode & 0x0F00) >> 8] >> 7;
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] <<= 1;
                    
                    chip8->programCounter += 2;
                    break;
                }

                default:
                {
                    printf("Unknown opcode %.4X\n", chip8->opcode);
                    exit(5);
                }
            }

            break;
        }

        case 0x9000: // opcode 9XY0: stops the next instruction if registers[x] does not equal registers[y], skipping the next instruction if it passes
        {
            if (chip8->registers[(chip8->opcode & 0x0F00) >> 8] != chip8->registers[(chip8->opcode & 0x00F0) >> 4])
            {
                chip8->programCounter += 4;
                break;
            }

            chip8->programCounter += 2;
            break;
        }

        case 0xA000: // opcode ANNN: set the index register to the address NNN
        {
            chip8->indexRegister = chip8->opcode & 0x0FFF; // 0x0FFF = 0b0000 1111 1111 1111 (which masks out the leftmost 4 bits, leaving NNN)
            chip8->programCounter += 2;                    // increment the program counter by 2 (so that it points to the next instruction in memory)

            break;
        }

        case 0xB000: // opcode BNNN: jump to the address NNN plus register[0]
        {
            chip8->programCounter = chip8->opcode & 0x0FFF + chip8->registers[0];
            break;
        }

        case 0xC000: // opcode CXNN: set registers[x] to rand() & NN
        {
            chip8->registers[(chip8->opcode & 0x0F00) >> 8] = rand() & (chip8->opcode & 0xFF);
            chip8->programCounter += 2;
            break;
        }

        /*
            opcode DXYN: draws a sprite at coordinate (registers[x], registers[y]) that has a width of 8 pixels and a height of N pixels
            each row (which is 8 pixels wide) is read as bit-coded (i.e., bit of 1 means we should draw, and a bit of 0 means to leave it blank)
            starting from the memory location stored in the index register. this means that we will be searching from:
                memory[index register] through memory[index register + 8 * N]

            additionally, all pixels are set using the XOR operation. if any pixels go from set to unset, the carry register is set to 1, 
            and otherwise is set to 0
        */
        case 0xD000:
        {
            // set the carry register by default to 0
            chip8->carryRegister = 0;

            // note that for xpos and ypos, we modulo them by the width and height respectively as to prevent the sprites from being drawn wrapped around the screen
            DoubleByte xpos = chip8->registers[(chip8->opcode & 0x0F00) >> 8];
            DoubleByte ypos = chip8->registers[(chip8->opcode & 0x00F0) >> 4];
            DoubleByte height = chip8->opcode & 0x000F;
            DoubleByte spriteRowData;

            // iterate through each row
            for (int row = 0; row < height; row++)
            {
                // fetch the pixel for the given row
                spriteRowData = chip8->memory[chip8->indexRegister + row];

                // iterate through each bit of the pixel
                for (int column = 0; column < 8; column++)
                {
                    // if we are trying to draw the pixel off the screen, disallow it
                    if (xpos + column > SCREEN_WIDTH || ypos + row > SCREEN_HEIGHT)
                    {
                        break;
                    }

                    // check if the current evaluated pixel is set (note that 0x80 = 128, and looks like 0b1000 0000)
                    if ((spriteRowData & (0x80 >> column)) != 0)
                    {
                        // check if the pixel on the display is set
                        // xpos + column + (ypos + row) * 64 gets the index of the pixel in the array (of 2048 total pixels)
                        if (chip8->pixels[xpos + column + ((ypos + row) * 64)] == 1)
                        {
                            chip8->carryRegister = 1;
                        }

                        // set the pixel value using xor
                        chip8->pixels[xpos + column + ((ypos + row) * 64)] ^= 1;
                    }
                }
            }

            chip8->drawFlag = true; // set the draw flag to 1 (indicating that we need to update the screen)
            chip8->programCounter += 2;
            break;
        }

        case 0xE000: // for opcodes EX_
        {
            switch (chip8->opcode & 0xFF)
            {
                case 0x9E: // opcode EX9E: skips the next instruction if they key stored in registers[x] is pressed
                {
                    if (chip8->keys[chip8->registers[(chip8->opcode & 0x0F00) >> 8]])
                    {
                        chip8->programCounter += 4;
                        break;
                    }

                    chip8->programCounter += 2;
                    break;
                }

                case 0xA1: // opcode EXA1: skips the next instruction if the key stored in register x is not pressed
                {
                    if (!chip8->keys[chip8->registers[(chip8->opcode & 0x0F00) >> 8]])
                    {
                        chip8->programCounter += 4;
                        break;
                    }

                    chip8->programCounter += 2;
                    break;
                }
            }
            
            break;
        }

        case 0xF000: // for opcodes FX_
        {
            switch (chip8->opcode & 0xFF)
            {
                case 0x07: // opcode FX07: sets registers[x] to the value of the delay timer
                {   
                    chip8->registers[(chip8->opcode & 0x0F00) >> 8] = chip8->delayTimer;
                    chip8->programCounter += 2;
                    break;
                }

                case 0x0A: // opcode FX0A: awaits a key press, and will halt the process until it receives one (so we will not be updating the pc)
                {
                    // flag for if a key is pressed
                    bool keyPressed = false;

                    // iterate over the total number of keys, checking if any one of them are pressed
                    for (int key = 0; key < NUM_OF_KEYS; key++)
                    {
                        // check if the key is pressed
                        if (chip8->keys[key])
                        {
                            // go to the next instruction if the key is pressed
                            chip8->programCounter += 2;

                            // set the flag to true
                            keyPressed = true;

                            // store the key that is pressed in registers[x]
                            chip8->registers[(chip8->opcode & 0x0F00) >> 8] = key;
                        }
                    }

                    // if there was no key pressed, return immediately until the next cycle is emulated. returning immediately 
                    // means that the timers will not be updated
                    if (!keyPressed)
                        return;

                    break;
                }

                case 0x15: // opcode FX15: sets the delay timer to registers[x]
                {
                    chip8->delayTimer = chip8->registers[(chip8->opcode & 0x0F00) >> 8];
                    chip8->programCounter += 2;
                    break;
                }

                case 0x18: // opcode FX18: sets the sound timer to registers[x]
                {
                    chip8->soundTimer = chip8->registers[(chip8->opcode & 0x0F00) >> 8];
                    chip8->programCounter += 2;
                    break;
                }

                case 0x1E: // opcode FX1E: adds registers[x] to the index register
                {
                    chip8->indexRegister += chip8->registers[(chip8->opcode & 0x0F00) >> 8];
                    chip8->programCounter += 2;
                    break;
                }

                case 0x29: // opcode FX29: sets the index register equal to the location of the sprite for the character whose address is stored in registers[x]
                {
                    chip8->indexRegister = chip8->registers[(chip8->opcode & 0x0F00) >> 8] * 0x5;
                    chip8->programCounter += 2;
                    break;
                }

                /*
                    opcode FX33: stores the binary coded decimal representation of registers[x] by storing:
                        the hundreds digit at the address of the index register
                        the tens digit at the address of the index register + 1
                        the ones digit at the address of the index register + 2
                */
                case 0x33:
                {
                    chip8->memory[chip8->indexRegister]     = chip8->registers[(chip8->opcode & 0x0F00) >> 8] / 100;
                    chip8->memory[chip8->indexRegister + 1] = (chip8->registers[(chip8->opcode & 0x0F00) >> 8] / 10) % 10;
                    chip8->memory[chip8->indexRegister + 2] = chip8->registers[(chip8->opcode & 0x0F00) >> 8] % 10;

                    chip8->programCounter += 2;
                    break;
                }

                case 0x55: // opcode FX55: stores the values from registers[0] through registers[x] into memory starting at the address in the index register
                {
                    for (int r = 0; r <= (chip8->opcode & 0x0F00) >> 8; r++)
                    {
                        chip8->memory[chip8->indexRegister + r] = chip8->registers[r];
                    }

                    chip8->programCounter += 2;
                    break;
                }

                case 0x65: // opcode FX66: fills registers[0] through registers[x] with values starting from the address stored in the index register, incrementing 1 for each register
                {
                    for (int r = 0; r <= (chip8->opcode & 0x0F00) >> 8; r++)
                    {
                        chip8->registers[r] = chip8->memory[chip8->indexRegister + r];
                    }

                    chip8->programCounter += 2;
                    break;
                }

                default: 
                {
                    printf("Unknown opcode [0xF000]: 0x%X\n", chip8->opcode);
                    exit(5);
                }
            }
            
            break;
        }
    }
}

// this function will only be called once every 1/60th of a second and will update the 
// timers (that is, it will decrement the timers if they are above 0)
void updateChip8Timers(chip8* chip8)
{
    if (chip8->delayTimer > 0)
        chip8->delayTimer--;

    if (chip8->soundTimer > 0)
    {
        if (chip8->soundTimer == 1)
            chip8->soundFlag = true;

        chip8->soundTimer--;
    }
}