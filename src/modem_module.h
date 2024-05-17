#ifndef MODEM_MODULE_H__
#define MODEM_MODULE_H__

void lte_handler(const struct lte_lc_evt *const evt);

int modem_mod_init(void);

int modem_mod_connect(void);

#endif
