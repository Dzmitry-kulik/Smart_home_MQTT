import matplotlib.dates as mdates
from datetime import datetime, timedelta

from matplotlib import pyplot as plt


def create_iot_graph(db_conn ,column_name, title, color):
    cursor = db_conn.cursor()

    # Берем данные за последние 24 часа
    query = f"SELECT ts, {column_name} FROM sensors WHERE ts >= datetime('now', '-24 hours', 'localtime') ORDER BY ts ASC"
    cursor.execute(query)
    data = cursor.fetchall()

    if not data:
        return None  # Если данных нет, вернем пустоту

    # Обработка данных
    # Замени свою строку на эту:
    times = [datetime.strptime(d[0].split('.')[0], '%Y-%m-%d %H:%M:%S') for d in data]

    values = [d[1] for d in data]

    # Настройка графиков
    now = datetime.now()
    start_time = now - timedelta(hours=24)

    plt.figure(figsize=(10, 5))
    plt.plot(times, values, color=color, linewidth=2, label=title)

    # Жесткий таймлайн на 24 часа
    plt.xlim(start_time, now)

    # Сетка mdates
    ax = plt.gca()
    ax.xaxis.set_major_locator(mdates.HourLocator(interval=2))
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
    ax.xaxis.set_minor_locator(mdates.MinuteLocator(interval=10))

    plt.grid(True, which='major', linestyle='-', alpha=0.6)
    plt.grid(True, which='minor', linestyle=':', alpha=0.3)

    plt.title(f"{title} (За 24 часа)")
    plt.gcf().autofmt_xdate()
    plt.tight_layout()

    filename = f"graph_{column_name}.png"
    plt.savefig(filename)
    plt.close()
    return filename
