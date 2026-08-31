#include <stdio.h>
#include <stdlib.h>

#include "ds_arena.h"
#include "ds_string.h"

int main(void) {
  _ds_arena_t_ arena = { 0 };

  // open and read the content of a file.
  FILE *fp = fopen("../../string-date.txt", "r");
  if ( fp == NULL ) {
    fputs("failed to open string-date.txt file", stderr);
    return EXIT_FAILURE;
  }
  fseek( fp, 0, SEEK_END );
  long fp_size = ftell( fp );
  rewind( fp );

  char* fp_buf = ( char * ) ds_arena_alloc_raw( &arena, fp_size );
  if ( fp_buf == NULL ) {
    fputs( "failed to allocate memory", stderr );
    fclose( fp );
    return EXIT_FAILURE;
  }
  fread( fp_buf, sizeof( char ), fp_size, fp );
  fclose(fp);

  printf(" text read: %s\n", fp_buf);

  ds_arena_destroy(&arena);

  return EXIT_SUCCESS;
}
