import paho.mqtt.client as mqtt
import telebot
import sqlite3
import json
import matplotlib.pyplot as plt
from datetime import datetime, timedelta
import threading
import matplotlib.dates as mdates
import os
import csv
import tempfile

from auxilariary_functions import create_iot_graph

TOKEN = ''
CHAT_ID = ''
MQTT_BROKER = "broker.emqx.io"
TOPIC_DATA = "myhome/stm32/data"
TOPIC_CMD = "myhome/stm32/commands"

bot = telebot.TeleBot(TOKEN)
bot_messages = []


def save_bot_message(message):
    if message and hasattr(message, 'message_id'):
        bot_messages.append(message.message_id)


def clear_chat(chat_id):
    global bot_messages
    deleted = 0
    for msg_id in bot_messages:
        try:
            bot.delete_message(chat_id, msg_id)
            deleted += 1
        except Exception as e:
            print(f"Не удалось удалить сообщение {msg_id}: {e}")
    bot_messages.clear()
    msg = bot.send_message(chat_id, f"✅ Очищено {deleted} сообщений бота.")
    save_bot_message(msg)

    def delete_later():
        try:
            bot.delete_message(chat_id, msg.message_id)
            if msg.message_id in bot_messages:
                bot_messages.remove(msg.message_id)
        except:
            pass

    threading.Timer(3.0, delete_later).start()


# --- ИНИЦИАЛИЗАЦИЯ БД ---
def init_db():
    conn = sqlite3.connect('sensors_data.db', check_same_thread=False)
    cursor = conn.cursor()
    cursor.execute('''CREATE TABLE IF NOT EXISTS sensors
                      (
                          ts
                          TIMESTAMP,
                          temp
                          REAL,
                          hum
                          REAL,
                          stove
                          REAL,
                          light
                          INTEGER,
                          smoke
                          INTEGER,
                          servo
                          INTEGER
                      )''')
    conn.commit()
    return conn


db_conn = init_db()


# --- ЛОГИКА MQTT (ПРИЕМ ДАННЫХ) ---
def on_message(client, userdata, message):
    try:
        payload = message.payload.decode("utf-8")
        data = json.loads(payload)
        print(f"Получено сообщение: {data}")

        # СЦЕНАРИЙ 1: Это экстренное сообщение о задымлении от STM32
        if 'alert' in data:
            if data['alert'] == "SMOKE_DETECTED":
                alert_msg = bot.send_message(
                    CHAT_ID,
                    f"‼️ КРИТИЧЕСКАЯ ТРЕВОГА: Обнаружен дым! Уровень: {data.get('level', 100)}%. "
                    f"Система безопасности активирована."
                )
                save_bot_message(alert_msg)
            return  # Выходим из функции, чтобы не писать этот пакет в таблицу датчиков

        # СЦЕНАРИЙ 2: Это обычные метрики от датчиков
        cursor = db_conn.cursor()
        now_str = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # Безопасный сбор данных с помощью .get()
        temp = data.get('t', 0.0)
        hum = data.get('h', 0.0)
        light = data.get('l', 0)
        smoke_percent = data.get('mq2_percent', 0)
        servo = data.get('sv', 90)  # В JSON от STM32 угол сервопривода пересылается как 'sv'

        # В вашей прошивке STM32 нет датчика температуры плиты, временно пишем 0.0
        stove_temp = 0.0

        cursor.execute("INSERT INTO sensors VALUES (?, ?, ?, ?, ?, ?, ?)",
                       (now_str, temp, hum, stove_temp, light, smoke_percent, servo))
        db_conn.commit()

        # Дублирующий контроль задымления по регулярному пакету данных
        if smoke_percent > 30:
            alert_msg = bot.send_message(CHAT_ID, f"⚠️ Предупреждение: Повышенный уровень CO/дыма: {smoke_percent}%!")
            save_bot_message(alert_msg)

        # Контроль утреннего/дневного освещения
        if datetime.now().hour >= 13 and light > 500:
            warn_msg = bot.send_message(CHAT_ID, "💡 Внимание: Свет на кухне горит, хотя уже день.")
            save_bot_message(warn_msg)

    except json.JSONDecodeError:
        print(f"Ошибка: Получена строка, не являющаяся валидным JSON: {message.payload}")
    except Exception as e:
        print(f"Ошибка парсинга: {e}")


# --- Функция экспорта данных в CSV с сортировкой ---
def export_to_csv(sort_by, order):
    cursor = db_conn.cursor()
    allowed_columns = ['ts', 'temp', 'hum', 'light']
    if sort_by not in allowed_columns:
        sort_by = 'ts'
    order = 'ASC' if order == 'возрастанию' else 'DESC'
    query = f"SELECT * FROM sensors ORDER BY {sort_by} {order}"
    cursor.execute(query)
    rows = cursor.fetchall()
    if not rows:
        return None

    fd, temp_path = tempfile.mkstemp(suffix='.csv')
    with os.fdopen(fd, 'w', newline='', encoding='utf-8') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(['Timestamp', 'Temperature', 'Humidity', 'Stove Temp',
                         'Light', 'Smoke', 'Servo'])
        writer.writerows(rows)
    return temp_path


def main_menu():
    markup = telebot.types.ReplyKeyboardMarkup(resize_keyboard=True)
    markup.add("📊 Статус сейчас")
    markup.add("🔥 Включить плиту", "❄️ Выключить плиту")
    markup.add("📈 Темп. воздуха", "📈 Влажность")
    markup.add("📈 Освещенность", "📥 Скачать таблицу")
    markup.add("🧹 Очистить чат")
    return markup


@bot.message_handler(commands=['start'])
def start_command(message):
    msg = bot.send_message(message.chat.id, "Выберите действие:", reply_markup=main_menu())
    save_bot_message(msg)


@bot.message_handler(func=lambda message: message.text == "📊 Статус сейчас")
def send_status(message):
    cursor = db_conn.cursor()
    cursor.execute("SELECT * FROM sensors ORDER BY ts DESC LIMIT 1")
    r = cursor.fetchone()
    if r:
        msg_text = (f"🏠 Воздух: {r[1]}°C / {r[2]}%\n"
                    f"🍳 Плита: {r[3]}°C\n"
                    f"💡 Свет: {r[4]}\n"
                    f"💨 Дым: {'ОПАСНО! (' + str(r[5]) + '%)' if r[5] > 30 else 'Норма (' + str(r[5]) + '%)'}\n"
                    f"🛡 Кран серво: {r[6]}°")
        msg = bot.send_message(message.chat.id, msg_text)
        save_bot_message(msg)


@bot.message_handler(func=lambda m: m.text == "📈 Темп. воздуха")
def graph_air(message):
    file = create_iot_graph(db_conn, "temp", "Температура воздуха", "blue")
    send_result(message, file)


@bot.message_handler(func=lambda m: m.text == "📈 Влажность")
def graph_hum(message):
    file = create_iot_graph(db_conn, "hum", "Влажность воздуха", "green")
    send_result(message, file)


@bot.message_handler(func=lambda m: m.text == "📈 Освещенность")
def graph_light(message):
    file = create_iot_graph(db_conn, "light", "Уровень света", "orange")
    send_result(message, file)


def send_result(message, filename):
    if filename:
        with open(filename, 'rb') as f:
            msg = bot.send_photo(message.chat.id, f)
            save_bot_message(msg)
        os.remove(filename)
    else:
        msg = bot.send_message(message.chat.id, "За последние 24 часа данных не найдено.")
        save_bot_message(msg)


# --- Экспорт таблицы с сортировкой ---
user_sort_state = {}


@bot.message_handler(func=lambda m: m.text == "📥 Скачать таблицу")
def start_export(message):
    markup = telebot.types.ReplyKeyboardMarkup(resize_keyboard=True, row_width=2)
    buttons = [telebot.types.KeyboardButton(col) for col in ['ts', 'temp', 'hum', 'light']]
    markup.add(*buttons)
    markup.add("🔙 Назад")
    msg = bot.send_message(message.chat.id, "Выберите поле для сортировки:", reply_markup=markup)
    save_bot_message(msg)
    user_sort_state[message.chat.id] = {'step': 'field'}


@bot.message_handler(func=lambda m: m.text in ['ts', 'temp', 'hum', 'light'] and
                                    user_sort_state.get(m.chat.id, {}).get('step') == 'field')
def choose_sort_field(message):
    field = message.text
    user_sort_state[message.chat.id]['field'] = field
    user_sort_state[message.chat.id]['step'] = 'order'
    markup = telebot.types.ReplyKeyboardMarkup(resize_keyboard=True)
    markup.add("по возрастанию", "по убыванию")
    markup.add("🔙 Назад")
    msg = bot.send_message(message.chat.id, f"Поле: {field}\nТеперь выберите порядок сортировки:", reply_markup=markup)
    save_bot_message(msg)


@bot.message_handler(func=lambda m: m.text in ['по возрастанию', 'по убыванию'] and
                                    user_sort_state.get(m.chat.id, {}).get('step') == 'order')
def choose_sort_order(message):
    order = message.text.split()[1]
    field = user_sort_state[message.chat.id]['field']
    csv_path = export_to_csv(field, order)
    if csv_path:
        with open(csv_path, 'rb') as f:
            doc_msg = bot.send_document(message.chat.id, f, caption=f"Данные, отсортированные по {field} ({order})")
            save_bot_message(doc_msg)
        os.remove(csv_path)
    else:
        err_msg = bot.send_message(message.chat.id, "Нет данных для экспорта.")
        save_bot_message(err_msg)
    user_sort_state.pop(message.chat.id, None)
    back_msg = bot.send_message(message.chat.id, "Возврат в главное меню", reply_markup=main_menu())
    save_bot_message(back_msg)


# --- Очистка чата ---
@bot.message_handler(func=lambda m: m.text == "🧹 Очистить чат")
def handle_clear_chat(message):
    clear_chat(message.chat.id)


# --- Управление плитой (меню режимов) ---
def stove_modes_menu():
    markup = telebot.types.ReplyKeyboardMarkup(resize_keyboard=True, row_width=3)
    buttons = [telebot.types.KeyboardButton(f"Режим {i}") for i in range(1, 7)]
    markup.add(*buttons)
    markup.add("🔙 Назад")
    return markup


@bot.message_handler(func=lambda m: m.text == "🔥 Включить плиту")
def stove_on(message):
    msg = bot.send_message(message.chat.id, "Выберите режим мощности (1-6):", reply_markup=stove_modes_menu())
    save_bot_message(msg)


@bot.message_handler(func=lambda m: m.text.startswith("Режим "))
def set_stove_mode(message):
    mode = message.text.split()[-1]
    mqtt_client.publish(TOPIC_CMD, f"STOVE_MODE:{mode}")
    msg = bot.send_message(message.chat.id, f"✅ Установлен {message.text}. Команда отправлена.",
                           reply_markup=main_menu())
    save_bot_message(msg)


@bot.message_handler(func=lambda m: m.text == "❄️ Выключить плиту")
def stove_off(message):
    mqtt_client.publish(TOPIC_CMD, "STOVE_OFF")
    msg = bot.send_message(message.chat.id, "❄️ Плита выключена", reply_markup=main_menu())
    save_bot_message(msg)


@bot.message_handler(func=lambda m: m.text == "🔙 Назад")
def back_to_main(message):
    user_sort_state.pop(message.chat.id, None)
    msg = bot.send_message(message.chat.id, "Возврат в главное меню", reply_markup=main_menu())
    save_bot_message(msg)


# --- ЗАПУСК ---
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER)
mqtt_client.subscribe(TOPIC_DATA)

# Запуск цикла MQTT в отдельном потоке
threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()

# Запуск Телеграм бота (блокирующий вызов)
bot.polling(none_stop=True)