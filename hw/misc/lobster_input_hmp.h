/* HMP injection API for the LobsterOS input device */
#ifndef LOBSTER_INPUT_HMP_H
#define LOBSTER_INPUT_HMP_H

void lobster_input_hmp_mouse(int x, int y);
/*
 * Click as a button mask: bit0 left, bit1 right, bit2 middle; 0 = release
 * all buttons. Masks beyond 0x07 are ignored.
 */
void lobster_input_hmp_click(int press);
/*
 * Mouse wheel: |dy| notches (+ up, - down) then |dx| notches (+ right,
 * - left), each clamped to 10, at the current pointer position.
 */
void lobster_input_hmp_wheel(long dy, long dx);
void lobster_input_hmp_key(uint8_t scancode);
/* Key press+release with modifier flags (bit0=shift, bit1=ctrl, bit2=alt). */
void lobster_input_hmp_key_flags(uint8_t scancode, uint8_t flags);

#endif
