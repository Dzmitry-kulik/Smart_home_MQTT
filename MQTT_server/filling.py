import sqlite3
import random
from datetime import datetime, timedelta

def fill_sensors_with_realistic_data(num_records=100):
    """
    Генерирует num_records записей и вставляет их в таблицу sensors.
    Данные реалистичны: температура, влажность, свет (в зависимости от времени суток),
    случайные эпизоды работы плиты и задымления.
    """
    conn = sqlite3.connect('sensors_data.db')
    cursor = conn.cursor()

    # Очистим таблицу (раскомментируйте, если нужно заменить старые данные)
    # cursor.execute("DELETE FROM sensors")
    # conn.commit()

    now = datetime.now()
    # Равномерно распределим записи за последние 7 дней (измените при необходимости)
    start_time = now - timedelta(days=7)

    for i in range(num_records):
        # Временная метка: равномерно между start_time и now
        ts = start_time + (now - start_time) * (i / num_records)
        ts_str = ts.strftime('%Y-%m-%d %H:%M:%S')
        hour = ts.hour

        # Температура (°C): нормальное распределение ~22°C, сезонность (утром прохладнее)
        temp = 22 + 3 * random.gauss(0, 1) + 2 * abs(hour - 14) / 14 - 2
        temp = max(15, min(35, temp))

        # Влажность (%): обратно пропорциональна температуре + случайность
        hum = 60 - 0.5 * (temp - 22) + random.uniform(-10, 10)
        hum = max(30, min(85, hum))

        # Освещённость (люкс): днём (7..20) 200..800, ночью 0..50
        if 7 <= hour <= 20:
            light = random.randint(200, 800)
        else:
            light = random.randint(0, 50)

        # Плита: чаще выключена (0), иногда случайные включения (100..250)
        stove = 0
        if random.random() < 0.15:   # 15% времени плита работает
            stove = random.randint(100, 250)

        # Дым: случайно 1 (тревога) с низкой вероятностью (3%)
        smoke = 1 if random.random() < 0.03 else 0

        # Серво (угол): 0 (закрыт) или 90 (открыт), зависит от состояния плиты
        servo = 90 if stove > 0 else 0

        cursor.execute('''INSERT INTO sensors (ts, temp, hum, stove, light, smoke, servo)
                          VALUES (?, ?, ?, ?, ?, ?, ?)''',
                       (ts_str, round(temp, 1), round(hum, 1), stove, light, smoke, servo))

    conn.commit()
    conn.close()
    print(f"Вставлено {num_records} записей.")

if __name__ == "__main__":
    fill_sensors_with_realistic_data(100)