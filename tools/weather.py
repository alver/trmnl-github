"""
TRMNL Weather
Port of ERB template to Python + Jinja2.

Deps:
    pip install requests jinja2 playwright
    playwright install chromium

Launch:
    python weather.py
"""

import requests
import subprocess
from pathlib import Path
from datetime import datetime
from jinja2 import Template

# Configs
LAT      = 48.1351
LON      = 11.5820
TIMEZONE = "Europe/Berlin"
CITY     = "München"
BASE_URL = "https://trmnl.com"

# WMO code: SVG icon name
WMO_TO_ICON = {
    0:  "wi-day-sunny",
    1:  "wi-day-sunny-overcast",
    2:  "wi-day-cloudy",
    3:  "wi-cloudy",
    45: "wi-day-fog",
    48: "wi-day-fog",
    51: "wi-day-sprinkle",
    53: "wi-day-sprinkle",
    55: "wi-day-rain",
    61: "wi-day-rain",
    63: "wi-day-rain",
    65: "wi-day-rain-wind",
    66: "wi-day-sleet",
    67: "wi-day-sleet",
    71: "wi-day-snow",
    73: "wi-day-snow",
    75: "wi-day-snow-wind",
    77: "wi-day-hail",
    80: "wi-day-showers",
    81: "wi-day-showers",
    82: "wi-day-storm-showers",
    85: "wi-day-snow",
    86: "wi-day-snow-wind",
    95: "wi-day-thunderstorm",
    96: "wi-day-sleet-storm",
    99: "wi-day-sleet-storm",
}

WMO_TO_DESC = {
    0:  "Clear sky",
    1:  "Mainly clear",
    2:  "Partly cloudy",
    3:  "Overcast",
    45: "Fog",
    48: "Icy fog",
    51: "Light drizzle",
    53: "Drizzle",
    55: "Heavy drizzle",
    61: "Light rain",
    63: "Rain",
    65: "Heavy rain",
    71: "Light snow",
    73: "Snow",
    75: "Heavy snow",
    77: "Snow grains",
    80: "Showers",
    81: "Showers",
    82: "Heavy showers",
    85: "Snowfall",
    86: "Heavy snowfall",
    95: "Thunderstorm",
    96: "Thunderstorm w/ hail",
    99: "Thunderstorm w/ hail",
}

DAYS_EN   = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
MONTHS_EN = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]


def icon_url(name):
    return f"{BASE_URL}/images/plugins/weather/{name}.svg"


def fmt_time(iso_str):
    return iso_str[11:16] if iso_str else "—"


def fetch_weather():
    r = requests.get(
        "https://api.open-meteo.com/v1/forecast",
        params={
            "latitude":  LAT,
            "longitude": LON,
            "current": [
                "temperature_2m", "apparent_temperature",
                "weathercode", "windspeed_10m",
                "relative_humidity_2m",
            ],
            "daily": [
                "weathercode",
                "temperature_2m_max", "temperature_2m_min",
                "uv_index_max",
                "precipitation_probability_max",
                "sunrise", "sunset",
            ],
            "timezone":     TIMEZONE,
            "forecast_days": 3,
        },
        timeout=10,
    )
    r.raise_for_status()
    return r.json()


def build_context(data):
    cur   = data["current"]
    daily = data["daily"]
    now   = datetime.now()

    wcode = cur["weathercode"]

    def day_ctx(i):
        d = datetime.fromisoformat(daily["time"][i])
        c = daily["weathercode"][i]
        return {
            "icon":       icon_url(WMO_TO_ICON.get(c, "wi-na")),
            "conditions": WMO_TO_DESC.get(c, "—"),
            "maxtemp":    round(daily["temperature_2m_max"][i]),
            "mintemp":    round(daily["temperature_2m_min"][i]),
            "uv_index":   daily["uv_index_max"][i],
            "day_name":   DAYS_EN[d.weekday()],
        }

    return {
        "base_url":               BASE_URL,
        "city":                   CITY,
        "date_str":               f"{MONTHS_EN[now.month-1]} {now.day}, {DAYS_EN[now.weekday()]}",
        "temperature":            round(cur["temperature_2m"]),
        "feels_like":             round(cur["apparent_temperature"]),
        "humidity":               round(cur["relative_humidity_2m"]),
        "conditions":             WMO_TO_DESC.get(wcode, "—"),
        "weather_image":          icon_url(WMO_TO_ICON.get(wcode, "wi-na")),
        "today_weather_image":    icon_url(WMO_TO_ICON.get(daily["weathercode"][0], "wi-na")),
        "tomorrow_weather_image": icon_url(WMO_TO_ICON.get(daily["weathercode"][1], "wi-na")),
        "forecast": {
            "today":    day_ctx(0),
            "tomorrow": day_ctx(1),
        },
    }


# HTML template ported from original ERB
TEMPLATE = """\
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <link rel="stylesheet" href="{{ base_url }}/css/latest/plugins.css">
  <script src="{{ base_url }}/js/latest/plugins.js"></script>
  <style>
    .weather-image { width: 120px; height: 120px; }
    .weather-icon  { width: 28px;  height: 28px;  }
  </style>
</head>
<body class="environment trmnl">
<div class="screen">

<div class="view view--full">
  <div class="layout layout--col gap--space-between">

    <!-- Верхний блок: текущая погода -->
    <div class="grid">

      <div class="row row--center col--span-3 col--end">
        <img class="weather-image" src="{{ weather_image }}" />
      </div>

      <div class="col col--span-3 col--end">
        <div class="item h--full">
          <div class="meta"></div>
          <div class="content">
            <span class="value value--xxxlarge">{{ temperature }}°</span>
            <span class="label">{{ city }} · {{ date_str }}</span>
          </div>
        </div>
      </div>

      <div class="col col--span-3 col--end gap--medium">

        <div class="item">
          <div class="meta"></div>
          <div class="icon">
            <img class="weather-icon" src="{{ base_url }}/images/plugins/weather/wi-thermometer.svg" />
          </div>
          <div class="content">
            <span class="value value--small">{{ feels_like }}°</span>
            <span class="label">Feels like</span>
          </div>
        </div>

        <div class="item">
          <div class="meta"></div>
          <div class="icon">
            <img class="weather-icon" src="{{ base_url }}/images/plugins/weather/wi-humidity.svg" />
          </div>
          <div class="content">
            <span class="value value--small">{{ humidity }}%</span>
            <span class="label">Humidity</span>
          </div>
        </div>

        <div class="item">
          <div class="meta"></div>
          <div class="icon">
            <img class="weather-icon" src="{{ weather_image }}" />
          </div>
          <div class="content">
            <span class="value value--xsmall">{{ conditions }}</span>
            <span class="label">Right now</span>
          </div>
        </div>

      </div>
    </div>

    <div class="w-full b-h-gray-5"></div>

    <!-- Нижний блок: прогноз сегодня + завтра -->
    <div class="grid">
      <div class="col gap--large">

        {% for key in ['today', 'tomorrow'] %}
        {% set day = forecast[key] %}
        <div class="grid">

          <div class="item col--span-3">
            <div class="meta"></div>
            <div class="icon">
              <img class="weather-icon" src="{{ day.icon }}" />
            </div>
            <div class="content">
              <span class="value value--xsmall">{{ day.conditions }}</span>
              <span class="label">{{ 'Today' if key == 'today' else 'Tomorrow' }}</span>
            </div>
          </div>

          <div class="item col--span-3">
            <div class="meta"></div>
            <div class="icon">
              <img class="weather-icon" src="{{ base_url }}/images/plugins/weather/wi-hot.svg" />
            </div>
            <div class="content">
              <span class="value value--xsmall">{{ day.uv_index }}</span>
              <span class="label">UV index</span>
            </div>
          </div>

          <div class="row col--span-3">
            <div class="item">
              <div class="meta"></div>
              <div class="icon">
                <img class="weather-icon" src="{{ base_url }}/images/plugins/weather/wi-thermometer.svg" />
              </div>
              <div class="row">
                <div class="content w--20">
                  <span class="value value--small">{{ day.mintemp }}°</span>
                  <span class="label">Low</span>
                </div>
                <div class="content w--20">
                  <span class="value value--small">{{ day.maxtemp }}°</span>
                  <span class="label">High</span>
                </div>
              </div>
            </div>
          </div>

        </div>
        {% endfor %}

      </div>
    </div>

  </div>

  <div class="title_bar">
    <img class="image" src="{{ base_url }}/images/plugins/weather--render.svg" />
    <span class="title">Weather</span>
    <span class="instance">{{ city }}</span>
  </div>
</div>

</div>
</body>
</html>
"""


# Processing pipeline
def render_html(ctx, path="weather.html"):
    html = Template(TEMPLATE).render(**ctx)
    Path(path).write_text(html, encoding="utf-8")
    print(f"  HTML → {path}")
    return path


def screenshot(html_path, png_path="weather.png"):
    from playwright.sync_api import sync_playwright
    abs_path = Path(html_path).absolute().as_posix()
    with sync_playwright() as p:
        browser = p.chromium.launch()
        page = browser.new_page(viewport={"width": 800, "height": 480})
        page.goto(f"file:///{abs_path}")
        # download all SVG from trmnl.com
        page.wait_for_load_state("networkidle")
        page.wait_for_timeout(500)
        page.screenshot(path=png_path)
        browser.close()
    print(f"  PNG  → {png_path}")
    return png_path


def to_bmp(png_path, bmp_path="weather.bmp"):
    subprocess.run([
        "magick", png_path,
        "-monochrome",
        "-colors", "2",
        "-depth", "1",
        "-strip",
        f"bmp3:{bmp_path}",
    ], check=True)
    print(f"  BMP  → {bmp_path}")
    return bmp_path


def main():
    print("Fetching Open-Meteo data...")
    data = fetch_weather()
    ctx  = build_context(data)
    print(f"  {ctx['temperature']}°, {ctx['conditions']}, humidity {ctx['humidity']}%")
    print()
    print("Rendering...")
    html = render_html(ctx)
    png  = screenshot(html)
    bmp  = to_bmp(png)
    print()
    print("Done:")
    print(f"  Browser:  {html}")
    print(f"  Preview:  {png}")
    print(f"  Device:   {bmp}")


if __name__ == "__main__":
    main()