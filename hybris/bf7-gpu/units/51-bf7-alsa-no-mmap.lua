-- BF7: запрет отображения памяти для устройств ALSA.
--
-- Наше ядро роняется, когда обычная программа открывает звуковое устройство с
-- отображением памяти: проверено дважды, во второй раз перезагрузка случилась
-- ровно на запуске pipewire в сеансе (androidboot.bootreason=kernel_panic).
-- Поэтому звуковой сервер обязан работать через обычные чтение и запись.
--
-- Файл кладётся в /etc/wireplumber/main.lua.d/ — он читается после штатных
-- правил и перекрывает их.

alsa_monitor.rules = alsa_monitor.rules or {}

table.insert(alsa_monitor.rules, {
  matches = {
    { { "node.name", "matches", "alsa_output.*" } },
    { { "node.name", "matches", "alsa_input.*" } },
  },
  apply_properties = {
    ["api.alsa.disable-mmap"] = true,
    -- Просыпаться реже: на четырёх A53 мелкий период даёт заметную нагрузку.
    ["api.alsa.period-size"] = 1024,
    ["api.alsa.headroom"] = 8192,
  },
})
