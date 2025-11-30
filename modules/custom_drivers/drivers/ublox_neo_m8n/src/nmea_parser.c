/**
 * @file nmea_parser.c
 * @brief NMEA Sentence Parser
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ublox_neo_m8n.h"

LOG_MODULE_DECLARE(ublox_neo_m8n, CONFIG_UBLOX_NEO_M8N_LOG_LEVEL);

/* Parser helper functions */

static int hex_to_int(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool verify_checksum(const char *sentence)
{
    const char *p = sentence;
    uint8_t checksum = 0;
    int expected;

    /* Skip $ */
    if (*p == '$') p++;

    /* Calculate checksum (XOR of all chars between $ and *) */
    while (*p && *p != '*') {
        checksum ^= *p++;
    }

    /* Check for * */
    if (*p != '*') {
        return false;
    }
    p++;

    /* Parse expected checksum */
    int h1 = hex_to_int(p[0]);
    int h2 = hex_to_int(p[1]);

    if (h1 < 0 || h2 < 0) {
        return false;
    }

    expected = (h1 << 4) | h2;

    return checksum == expected;
}

static char *get_field(char *sentence, int field_num)
{
    char *p = sentence;
    int count = 0;

    while (*p && count < field_num) {
        if (*p == ',') {
            count++;
        }
        p++;
    }

    return p;
}

static double parse_coordinate(const char *str, char direction)
{
    if (!str || *str == '\0' || *str == ',') {
        return 0.0;
    }

    double value = atof(str);

    /* NMEA format: DDDMM.MMMM */
    int degrees = (int)(value / 100);
    double minutes = value - (degrees * 100);
    double result = degrees + (minutes / 60.0);

    if (direction == 'S' || direction == 'W') {
        result = -result;
    }

    return result;
}

static void parse_time(const char *str, struct gps_time *time)
{
    if (!str || strlen(str) < 6) {
        time->valid = false;
        return;
    }

    /* Format: HHMMSS.sss */
    time->hour = (str[0] - '0') * 10 + (str[1] - '0');
    time->minute = (str[2] - '0') * 10 + (str[3] - '0');
    time->second = (str[4] - '0') * 10 + (str[5] - '0');

    if (str[6] == '.') {
        time->millisec = atoi(&str[7]);
    } else {
        time->millisec = 0;
    }

    time->valid = true;
}

static void parse_date(const char *str, struct gps_time *time)
{
    if (!str || strlen(str) < 6) {
        return;
    }

    /* Format: DDMMYY */
    time->day = (str[0] - '0') * 10 + (str[1] - '0');
    time->month = (str[2] - '0') * 10 + (str[3] - '0');
    time->year = 2000 + (str[4] - '0') * 10 + (str[5] - '0');
}

/* ========== Sentence Parsers ========== */

/**
 * @brief Parse GGA sentence (GPS Fix Data)
 *
 * $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
 */
int nmea_parse_gga(char *sentence, struct gps_data *data)
{
    char *field;
    char dir;

    /* Time */
    field = get_field(sentence, 1);
    parse_time(field, &data->time);

    /* Latitude */
    field = get_field(sentence, 2);
    char *lat_dir = get_field(sentence, 3);
    dir = *lat_dir;
    data->position.latitude = parse_coordinate(field, dir);

    /* Longitude */
    field = get_field(sentence, 4);
    char *lon_dir = get_field(sentence, 5);
    dir = *lon_dir;
    data->position.longitude = parse_coordinate(field, dir);

    /* Fix quality */
    field = get_field(sentence, 6);
    data->fix_quality = atoi(field);

    /* Number of satellites */
    field = get_field(sentence, 7);
    data->satellites_used = atoi(field);

    /* HDOP */
    field = get_field(sentence, 8);
    if (*field && *field != ',') {
        data->accuracy.hdop = atof(field);
        data->accuracy.valid = true;
    }

    /* Altitude */
    field = get_field(sentence, 9);
    if (*field && *field != ',') {
        data->position.altitude = atof(field);
    }

    /* Geoid separation */
    field = get_field(sentence, 11);
    if (*field && *field != ',') {
        data->position.geoid_sep = atof(field);
    }

    data->position.valid = (data->fix_quality > 0);
    data->fix_valid = data->position.valid;

    return 0;
}

/**
 * @brief Parse RMC sentence (Recommended Minimum)
 *
 * $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,x.x,a*hh
 */
int nmea_parse_rmc(char *sentence, struct gps_data *data)
{
    char *field;
    char dir;

    /* Time */
    field = get_field(sentence, 1);
    parse_time(field, &data->time);

    /* Status */
    field = get_field(sentence, 2);
    bool valid = (*field == 'A');

    /* Latitude */
    field = get_field(sentence, 3);
    char *lat_dir = get_field(sentence, 4);
    dir = *lat_dir;
    data->position.latitude = parse_coordinate(field, dir);

    /* Longitude */
    field = get_field(sentence, 5);
    char *lon_dir = get_field(sentence, 6);
    dir = *lon_dir;
    data->position.longitude = parse_coordinate(field, dir);

    /* Speed (knots) */
    field = get_field(sentence, 7);
    if (*field && *field != ',') {
        data->velocity.speed_knots = atof(field);
        data->velocity.speed_kmh = data->velocity.speed_knots * 1.852f;
        data->velocity.speed_mps = data->velocity.speed_knots * 0.514444f;
        data->velocity.valid = true;
    }

    /* Course */
    field = get_field(sentence, 8);
    if (*field && *field != ',') {
        data->velocity.course = atof(field);
    }

    /* Date */
    field = get_field(sentence, 9);
    parse_date(field, &data->time);

    data->position.valid = valid;
    data->fix_valid = valid;

    return 0;
}

/**
 * @brief Parse GSA sentence (GPS DOP and Active Satellites)
 *
 * $GPGSA,A,3,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,xx,x.x,x.x,x.x*hh
 */
int nmea_parse_gsa(char *sentence, struct gps_data *data)
{
    char *field;

    /* Mode */
    field = get_field(sentence, 1);
    /* A = Automatic, M = Manual */

    /* Fix type */
    field = get_field(sentence, 2);
    int fix_type = atoi(field);
    if (fix_type == 1) {
        data->fix_type = GPS_FIX_NONE;
    } else if (fix_type == 2) {
        data->fix_type = GPS_FIX_2D;
    } else if (fix_type == 3) {
        data->fix_type = GPS_FIX_3D;
    }

    /* Skip satellite PRNs (fields 3-14) */

    /* PDOP */
    field = get_field(sentence, 15);
    if (*field && *field != ',') {
        data->accuracy.pdop = atof(field);
    }

    /* HDOP */
    field = get_field(sentence, 16);
    if (*field && *field != ',') {
        data->accuracy.hdop = atof(field);
    }

    /* VDOP */
    field = get_field(sentence, 17);
    if (*field && *field != ',') {
        data->accuracy.vdop = atof(field);
    }

    data->accuracy.valid = true;

    return 0;
}

/**
 * @brief Parse VTG sentence (Course and Speed)
 *
 * $GPVTG,x.x,T,x.x,M,x.x,N,x.x,K*hh
 */
int nmea_parse_vtg(char *sentence, struct gps_data *data)
{
    char *field;

    /* True course */
    field = get_field(sentence, 1);
    if (*field && *field != ',') {
        data->velocity.course = atof(field);
    }

    /* Skip magnetic course (fields 3-4) */

    /* Speed in knots */
    field = get_field(sentence, 5);
    if (*field && *field != ',') {
        data->velocity.speed_knots = atof(field);
    }

    /* Speed in km/h */
    field = get_field(sentence, 7);
    if (*field && *field != ',') {
        data->velocity.speed_kmh = atof(field);
        data->velocity.speed_mps = data->velocity.speed_kmh / 3.6f;
        data->velocity.valid = true;
    }

    return 0;
}

/**
 * @brief Parse NMEA sentence and update GPS data
 */
int nmea_parse_sentence(const char *sentence, struct gps_data *data)
{
    char buffer[128];
    size_t len;

    if (!sentence || !data) {
        return -EINVAL;
    }

    len = strlen(sentence);
    if (len < 10 || len >= sizeof(buffer)) {
        return -EINVAL;
    }

    /* Verify checksum */
    if (!verify_checksum(sentence)) {
        LOG_WRN("Checksum failed: %s", sentence);
        return -EILSEQ;
    }

    /* Copy to buffer for modification */
    strcpy(buffer, sentence);

    /* Identify sentence type */
    if (strstr(buffer, "GGA")) {
#ifdef CONFIG_UBLOX_NEO_M8N_PARSE_GGA
        return nmea_parse_gga(buffer, data);
#endif
    } else if (strstr(buffer, "RMC")) {
#ifdef CONFIG_UBLOX_NEO_M8N_PARSE_RMC
        return nmea_parse_rmc(buffer, data);
#endif
    } else if (strstr(buffer, "GSA")) {
#ifdef CONFIG_UBLOX_NEO_M8N_PARSE_GSA
        return nmea_parse_gsa(buffer, data);
#endif
    } else if (strstr(buffer, "VTG")) {
#ifdef CONFIG_UBLOX_NEO_M8N_PARSE_VTG
        return nmea_parse_vtg(buffer, data);
#endif
    }

    return 0;
}
