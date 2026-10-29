#include "screen_settings.h"

#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "qr_block.h"
#include "theme.h"
#include "ui_common.h"

namespace {

// Big enough to scan from arm's length without crowding the two address lines
// under it. The card adds its own quiet zone on top of this.
constexpr lv_coord_t QR_MODULES_PX = 168;

lv_obj_t* s_root = nullptr;
lv_obj_t* s_qr = nullptr;
lv_obj_t* s_host = nullptr;
lv_obj_t* s_ip = nullptr;
lv_obj_t* s_hint = nullptr;

// Remembered so a refresh four times a second does not re-encode a QR code that
// has not changed. lv_qrcode_update redraws the whole canvas.
char s_last_host[64] = {};
char s_last_ip[48] = {};

}  // namespace

lv_obj_t* screen_settings_create(lv_obj_t* parent) {
  s_root = puck_create_screen_root(parent);
  lv_obj_center(s_root);

  lv_obj_t* title = puck_create_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, -168);
  lv_label_set_text(title, "SETTINGS");

  s_qr = puck_qr_block_create(s_root, QR_MODULES_PX);
  lv_obj_align(s_qr, LV_ALIGN_CENTER, 0, -38);

  // The name first: it is the one worth remembering. The IP under it is what
  // actually works when mDNS does not — which is why it is set the same size and
  // the same brightness rather than demoted to a muted footnote. Both of these
  // exist to be read off the glass and typed into a phone, and the QR encodes the
  // IP precisely because it is the address least likely to fail.
  s_host = puck_create_label(s_root, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, 0, 86);
  lv_label_set_text(s_host, "");

  s_ip = puck_create_label(s_root, PUCK_FONT_BODY, PUCK_COLOUR_TEXT, 0, 116);
  lv_label_set_text(s_ip, "");

  s_hint = puck_create_label(s_root, PUCK_FONT_SMALL, PUCK_COLOUR_MUTED, 0, 152);
  lv_label_set_text(s_hint, "scan, or type it in a browser");

  screen_settings_set_address("", "");
  return s_root;
}

void screen_settings_set_address(const char* host, const char* ip) {
  if (s_root == nullptr) {
    return;
  }
  if (host == nullptr) {
    host = "";
  }
  if (ip == nullptr) {
    ip = "";
  }
  if (strncmp(s_last_host, host, sizeof(s_last_host)) == 0 &&
      strncmp(s_last_ip, ip, sizeof(s_last_ip)) == 0) {
    return;
  }
  snprintf(s_last_host, sizeof(s_last_host), "%s", host);
  snprintf(s_last_ip, sizeof(s_last_ip), "%s", ip);

  // No address yet — off WiFi, or still joining. Say so rather than showing a
  // code that leads nowhere.
  if (ip[0] == '\0') {
    puck_qr_block_set_url(s_qr, "");
    lv_label_set_text(s_host, "no address yet");
    lv_label_set_text(s_ip, "");
    lv_label_set_text(s_hint, "waiting for WiFi");
    return;
  }

  // The IP goes in the code, not the .local name: a QR is scanned by a phone,
  // and a phone is the device least likely to resolve mDNS.
  char url[80];
  snprintf(url, sizeof(url), "http://%s/", ip);
  puck_qr_block_set_url(s_qr, url);

  char line[80];
  if (host[0] != '\0') {
    snprintf(line, sizeof(line), "http://%s/", host);
    lv_label_set_text(s_host, line);
    lv_label_set_text(s_ip, url);
  } else {
    lv_label_set_text(s_host, url);
    lv_label_set_text(s_ip, "");
  }
  lv_label_set_text(s_hint, "scan, or type it in a browser");
}
