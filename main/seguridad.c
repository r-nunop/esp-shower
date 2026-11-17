#include "seguridad.h"
#include "config.h"

codigo_error_t seguridad_evaluar(const lecturas_t *l, estado_t e)
{
    bool calentando = (e == EST_PRECALENTAMIENTO || e == EST_DUCHA);

    if (l->temp_valida && l->temp_c >= TEMP_SEGURIDAD_C) return ERR_SOBRETEMP;
    if (calentando && !l->temp_valida) return ERR_SENSOR_TEMP;
    if (e == EST_PRECALENTAMIENTO && !l->nivel_ok) return ERR_NIVEL_BAJO;
    return ERR_NINGUNO;
}
