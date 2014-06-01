#include <string.h>
#include <boost/concept_check.hpp>
#include <stdint.h>
#include <climits>
#include <algorithm>

#include "mixer.h"

const int16_t MAX_INT_16 = SHRT_MAX;
const int16_t MIN_INT_16 = SHRT_MIN;
const unsigned int COUNT = 176;


void add(int16_t &output_number, int16_t number)
{
  int result = output_number + number;

  if (result >= MAX_INT_16)
    output_number = MAX_INT_16;
  else if (result <= MIN_INT_16)
    output_number = MIN_INT_16;
  else
    output_number = result;
}

void mixer(
  struct mixer_input* inputs, size_t n,  // tablica struktur mixer_input, po jednej strukturze na każdą
  // kolejkę FIFO w stanie ACTIVE
  void* output_buf,                      // bufor, w którym mikser powinien umieścić dane do wysłania
  size_t* output_size,                   // początkowo rozmiar output_buf, następnie mikser umieszcza
  // w tej zmiennej liczbę bajtów zapisanych w output_buf
  unsigned long tx_interval_ms           // wartość zmiennej TX_INTERVAL
)
{
  memset(output_buf, 0, *output_size);
  int16_t *output_int = (int16_t *) output_buf;
  size_t output_int_len = (*output_size) / 2;

  for(size_t input_nr = 0 ; input_nr < n ; input_nr++)
  {
    // data from input_nr client
    int16_t *client_buf = (int16_t *) (inputs + input_nr)->data;
    size_t client_buf_len = (inputs + input_nr)->len / 2;

    // where to stop
    size_t upTo = std::min(output_int_len, client_buf_len);
    (inputs + input_nr)->consumed = 2 * upTo;

    for(size_t pos = 0 ; pos < upTo ; pos++ )
    {
        add( *(output_int + pos), *(client_buf + pos) );
    }
  }
}