/* HMP injection API for the LobsterOS input device */
#ifndef LOBSTER_INPUT_HMP_H
#define LOBSTER_INPUT_HMP_H

void lobster_input_hmp_mouse(int x, int y);
void lobster_input_hmp_click(int press);
void lobster_input_hmp_key(uint8_t scancode);

#endif
