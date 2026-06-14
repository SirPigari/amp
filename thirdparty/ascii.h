#ifndef ASCII_H
#define ASCII_H
const char ascii_art[] =
"                AMP                 \n"
"                          NH2       \n"
"                           |        \n"
"       O              N-\\ / \\\\   \n"
"      | |           //   ||   N     \n"
"  HO - P - O \\      \\    /\\   |  \n"
"       |      |     N ---  \\_//    \n"
"       OH     |/-O-\\|       N      \n"
"              \\____/               \n"
"               |   |                \n"
"               OH  OH               \n";
const char ascii_art_note[] =
"                                    \n"
"     The amp project is licensed    \n"
"     under the MIT License          \n"
"     (c) Markofwitch 2026           \n"
"     amp is a Video Player and has  \n"
"     nothing to do with AMP         \n"
"     (Adenosine monophosphate)      \n"
"     This ASCII art is for fun      \n";

#include <stdio.h>

/// @brief Prints the amp ASCII art to the console.
/// This is just for fun and has no functional purpose in the media player.
/// @author markofwitch
/// @return void
void print_ascii_art(void) {
    puts(ascii_art);
    puts(ascii_art_note);
    return;
}

#endif /* ASCII_H */