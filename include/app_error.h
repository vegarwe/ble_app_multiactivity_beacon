#ifndef APP_ERROR_H__
#define APP_ERROR_H__

#define APP_ASSERT(COND)   do {} while (COND)
#define APP_ERR_CHECK(ERR) do {  while (ERR != NRF_SUCCESS) {} } while (0)

#endif // APP_ERROR_H__
