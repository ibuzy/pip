"""All of Pip's moods as a page: number, a picture, how to get it (in Russian, for the owner) - docs/list on the site
(GitHub Pages: /pip/list), in the panel's look (docs/pip.css), with a search and filters.

    py tools/emotions_table.py    # renders with build/pipsim.exe into build/moods (cached: delete a mood's files to retake it):
                                  # a picture, and a short film that loops; and every feeling's sounds
                                  # writes docs/list/index.html, img/<name>.webp, film/<name>.mp4, snd/<sound>.mp3 and names.js
                                  # (the Russian names and how-tos, for the debug page)

The film's loop: a scene's own period (LOOP), else the two moments of eight seconds that look most alike, 3 to 5 s apart.

The order and the names come from ui/moods.h (PIP_*); a mood added there and missing from HOW fails loudly."""
import json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, "build", "pipsim.exe")
SHOTS = os.path.join(ROOT, "build", "moods")
LIST = os.path.join(ROOT, "docs", "list")
OUT = os.path.join(LIST, "index.html")

# id: (имя по-русски, как получить)
HOW = {
    "neutral": ("Нейтральный", "Ничего не делать: он возвращается к нему, когда ничего не происходит. Чаще в «средний» день (палитра «так себе») и вечером."),
    "happy": ("Радость", "Хороший день (палитра «отлично», вечер). Отмылся под губкой (и «свежий» после мытья). Подключить к зарядке. Поиграть с ним, когда не скучает. Сам здоровается, если доверяет тебе. В ритм-игре попасть в такт. Палец «пойман» после погони."),
    "sad": ("Грусть", "Плохой день (палитра «плохо»). Мало внимания (любовь ниже ~22%, по очереди с «обижен»). Держать полным на зарядке очень долго. Проиграть ему. Долго дуть — замёрз («зябко»)."),
    "angry": ("Злость", "Голоден (еда ниже 10%). Ночью будили уведомлениями — проснётся злым. Лестница злости: много тычков, бросок, дёргание кабеля. Помыть, когда он и так чистый. Разрядить батарею до нуля — после включения злой."),
    "surprised": ("Удивление", "Перевернуть лицом вверх, когда он спал лицом вниз. Ку-ку (на миг лицом вниз и обратно). Начало чиха, отрыжки, пука, икоты. Тычок («а?»). Резко подуть. Бупнуть в нос. Тычок в бок."),
    "sleepy": ("Сонный", "Ночью (22–6) при энергии ниже 50%. Только что проснулся. Энергия ниже 25%. Покачать (колыбельная). Провести пальцем вниз по глазам. Нарисовать спираль внутрь, когда устал."),
    "asleep": ("Спит", "Положить экраном вниз на 2 с. Ночью засыпает сам (23–7, энергия ниже 60%). Экран погас по таймеру — дремлет. Иногда притворяется спящим (палец его выдаёт)."),
    "skeptical": ("Скептичный", "Палитра «так себе». Мыть, когда он и так совсем чистый (90%+), или тереть дальше, когда уже отмылся — первая ступень, потом раздражён и зол. Второй раз подряд дёрнули кабель («опять кабель?»). Тычок («серьёзно?»). Обиженный не принимает доброту. Подули — «хватит». Держать палец на глазу."),
    "curious": ("Любопытный", "Палитры хорошего и среднего дня. Идти с ним (много шагов — прогулка). «Твоя очередь» в ритм-игре. Щелчок пальцем по экрану. Сам оглядывается по сторонам."),
    "love": ("Любовь", "Долго гладить. «Привет!» в конце рождения. Мастер ритма в ритм-игре. Встреча после разлуки, если любит тебя. Вечерняя палитра хорошего дня."),
    "dizzy": ("Головокружение", "Сильно потрясти (или средне, но дольше 1.6 с). 10 нажатий нижней кнопки подряд («голова кругом»). Перевернуть 4+ раз подряд — накувыркался. Отпустить после спирали. Резкий щелчок пальцем."),
    "ouch": ("Ай!", "Ткнуть пальцем. Резко подуть. Ткнуть пальцем прямо в глаз — щурит его."),
    "stoned": ("Обдолбанный", "Редкая карта дня (9–21 ч, примерно раз в 10 дней): вечером на зарядке, когда весело, но мало энергии."),
    "pinkeye": ("Конъюнктивит", "Редкая карта дня: когда он грязный."),
    "nauseous": ("Тошнит", "Перекормить печеньем («объелся»). Болен (долго грязный) — по очереди с «болен»."),
    "sick": ("Болен", "Батарея почти пуста (ниже 4%). Две потребности на нуле. Долго грязный или простыл мокрым (подули после мытья). Сам кашляет."),
    "tired": ("Усталый", "Батарея ниже 10%. Энергия ниже 25%. Вечерняя палитра плохого дня."),
    "exhausted": ("Измотан", "Энергия ниже 12%."),
    "annoyed": ("Раздражён", "Голоден (еда ниже 22%). Много быстрых нажатий нижней кнопки («хватит»). Разбудить нажатиями. Тыкать, когда он уже раздражён. Подуть — «хватит». Качать, когда не хочет спать. Остаток обиды."),
    "furious": ("В ярости", "Очень голоден (второе лицо голода). Выше по лестнице злости: снова бросить, тыкать, дёргать кабель. Мыть чистого слишком долго — отшвырнёт губку."),
    "rage": ("Бешенство", "Ещё выше по лестнице злости (бросить/тыкать/дёргать кабель снова и снова). Отшвырнул губку."),
    "swearing": ("Ругается", "Лестница злости в плохой день: «надоел», бросок. Насильно кормить или мыть, когда он обижен."),
    "indignant": ("Возмущён", "Обыграть его в игре. Разбудить второй раз за несколько минут. Держать палец на глазу 3+ с. Сильный остаток обиды."),
    "belligerent": ("Воинственный", "Голоден (второе лицо). Встреча, когда он тебя не любит (любовь низкая)."),
    "hurt": ("Обижен", "Мало любви (ниже ~22%). После падения или броска. Держали полным на зарядке, потом сняли. Задёргали нижней кнопкой. Долго не заходили («дуется»). Остаток обиды."),
    "disgusted": ("Брезгливость", "Грязный (чистота ниже ~30%). Перекормить."),
    "suspicious": ("Подозрительный", "Палитра «так себе». Остаток обиды. Встреча при низком доверии («настороже»)."),
    "cynical": ("Циничный", "Скучно (веселье низкое, второе лицо). Палитры «так себе» и «плохо»."),
    "afraid": ("Испуг", "Громкий хлопок рядом. При низком доверии вздрагивает от пальца или толчка. Тряска — «страшно». Подуть, когда не доверяет. «Бу!» — разбудить притворщика."),
    "nervous": ("Нервничает", "Батарея ниже 20%. Давно не гулял (второе лицо). Подуть. Спираль — «неуютно». Два пальца на экран не на глаза, когда доверие низкое. Сам нервно оглядывается."),
    "despondent": ("Подавлен", "Любовь ниже 10% (второе лицо). Палитра плохого дня."),
    "lonely": ("Одинок", "Любовь ниже 10%. «Скучает по тебе» — сам. Вечерняя палитра плохого дня."),
    "gloomy": ("Хмурый", "Давно не гулял (прогулки ниже 10%). Грязный (чистота ниже 12%). Палитра плохого дня."),
    "dull": ("Отупение", "Энергия на нуле (второе лицо). Потрясти три раза за минуту («растрясли»). Сам «залипает»."),
    "shy": ("Застенчивый", "Тычок, когда сильно любит. Тап по щеке. Погладить по голове или щеке. Сам смущённо смотрит. Мирится после обиды («оттаивает»)."),
    "smug": ("Самодовольный", "Он выиграл у тебя в игре. Ты ошибся в ритм-игре. Тычок — «не замечаю». Палитра отличного дня. Сам."),
    "bored": ("Скучает", "Веселье ниже 12%. Не ответить в ритм-игре. Качать отдохнувшего. Сам вздыхает."),
    "shocked": ("Шок", "Очень громкий хлопок. Уронить. «Бу!»."),
    "dismayed": ("Растерян", "Пачка уведомлений сразу. Палитра плохого дня."),
    "puzzled": ("Озадачен", "Перевернуть вверх ногами больше чем на 1 с. Пропущенный звонок. Икота. Спираль — «что?». Сам «размышляет»."),
    "amazed": ("Изумление", "Палитры хорошего дня. На прогулке. Ку-ку. Рекорд в игре с ним. Сам «видит что-то». Пальцем раскрыть ему глаза."),
    "cheeky": ("Дерзкий", "Включить брелок (конец пробуждения). Палитра отличного дня. Тап под подбородком. Увернулся от пальца. Сам подмигивает. После пука."),
    "content": ("Довольный", "Палитры хорошего и среднего дня. Мыть, пока он не совсем чистый (жмурится от удовольствия). Погладить по голове или щеке. Покормить, когда сыт наполовину."),
    "comfy": ("Уютно", "На зарядке дольше минуты. Качать. Гладить по голове усталого. Вечерние палитры."),
    "yes": ("Да! (кивает)", "Позвать играть, когда ему скучно. Ответить на звонок с брелока. Привязать телефон."),
    "no": ("Нет (мотает головой)", "Позвать играть, когда устал, грязный или обижен. Предложить еду или мытьё, когда он отказывается. Сбросить звонок."),
    "allergic": ("Аллергия", "Редкая карта дня: весной после прогулок. Чихает."),
    "frozen": ("Замёрз", "Редкая карта дня: зимним утром. Или дуть на него 10 секунд."),
    "overheated": ("Перегрелся", "Редкая карта дня: летом в полдень, после прогулки или на зарядке."),
    "smitten": ("Влюблён по уши", "Редкая карта дня: после того как его много гладили."),
    "hypnotized": ("Загипнотизирован", "Редкая карта дня: после частых встряхиваний. Нарисовать спираль внутрь — «заворожён»."),
    "hyper": ("Гиперактивен", "Редкая карта дня: печенье за печеньем."),
    "zombie": ("Зомби", "Редкая карта дня: после ночи, когда его будили."),
    "excited": ("Восторг", "Покачать брелок из стороны в сторону, ~раз в секунду («уии!»). Слегка потрясти. День рождения владельца (песня)."),
    "dreamy": ("Мечтательный", "Положить палец на экран и не двигать. Мягко подуть («ветерок»). Сам напевает."),
    "pouty": ("Надулся", "Начать гладить и бросить три раза подряд («дразнят»). Ревнует к телефону (много уведомлений, музыка без него). Ещё обижен, когда гладят."),
    "proud": ("Гордый", "Пройти с ним дневную норму шагов."),
    "focused": ("Сосредоточен", "Медленно вести пальцем по экрану — следит за ним. Сам задумывается."),
    "touched": ("Растроган", "Долго гладить, когда он тебя любит. Помириться после обиды долгим поглаживанием."),
    "playful": ("Игривый", "Ку-ку (на миг лицом вниз и обратно). Водить пальцем по экрану, не отпуская (погоня). Тычок — «ещё!». Быстрый щелчок пальцем."),
    "relieved": ("Облегчение", "Перевернуть обратно после «вверх ногами». Натанцевался. Икота прошла. После «головы кругом» (10 нажатий). Убрать два пальца после «глаза разбегаются»."),
    "laughing": ("Смех", "Щекотать: быстро тереть пальцем в одной точке (где угодно, хоть на щеке). 2–3 быстрых нажатия нижней кнопки. Тычок в бок. Два пальца на экран не на глаза, когда ему очень весело."),
    "grateful": ("Благодарен", "Покормить (подержать нижнюю кнопку), когда голоден."),
    "dancing": ("Танцует", "Музыка с ритмом рядом с брелоком или на привязанном телефоне, если он в настроении."),
    "tumbling": ("Кувыркается", "Переворачивать брелок вверх ногами и обратно 4 раза за 4 секунды."),
    "berserk": ("Безумие", "Вершина лестницы злости в очень плохой день: бросать, тыкать, дёргать кабель снова и снова."),
    "whirled": ("Закружён", "Рисовать пальцем на экране спираль или круги."),
    "woozy": ("Ошалел", "Нарисовать пальцем на экране спираль или круги (2+ оборота) и отпустить палец."),
    "impatient": ("Нетерпеливый", "Повседневное лицо: ждёт тебя (палитра «так себе»). Сам."),
    "wistful": ("Задумчивый", "Повседневное лицо в средние и плохие дни, вечером. Сам «мечтает»."),
    "blissful": ("Блаженство", "Погладить под подбородком или по щеке. Повседневное лицо в хорошие дни. Сам."),
    "adoring": ("Обожает", "Повседневное лицо в отличный день. Сам «обожает тебя»."),
    "sly": ("Хитрый", "Сам «замышляет». Иногда после пука."),
    "sheepish": ("Виноватый", "После отрыжки или пука. Отшвырнул губку — «прости»."),
    "determined": ("Решительный", "Барабанит в ритм-игре. Приседает перед прыжком в погоне за пальцем."),
    "eureka": ("Ага!", "Сам: задумался — и придумал."),
    "eyeroll": ("Закатывает глаза", "Ткнуть, когда он раздражён. Сам, когда раздражён."),
    "flustered": ("Смущён", "Тап по щеке. Тычок в бок. Потереть щёку."),
    "squished": ("Сплющило", "Накрыть экран ладонью (всей, не пальцами) и подержать секунду. Убрать ладонь — продолжение сценки."),
    "stretched": ("Растянули", "Поставить по пальцу на каждый глаз и потянуть пальцы в любую сторону (в стороны, вверх, по диагонали) больше чем на ~1 см. Отпустить — продолжение сценки."),
    "overwhelmed": ("Глаза разбегаются", "Положить два пальца на экран не на глаза (например, внизу) и держать полсекунды и дольше. Держать дольше 1.5 с — вторая часть сценки. Иногда вместо него выпадает «нервничает» или «смех»."),
    "pinched": ("Глаза свели", "Поставить по пальцу на каждый глаз (ближе к внешним краям) и свести пальцы к середине больше чем на ~1 см. Отпустить — продолжение сценки."),
    "beaming": ("Сияет", "Начать гладить пальцем (вариант вместо радости, примерно раз из семи). Или поймать: водить пальцем по экрану, пока он гонится, и отпустить (почти в половине случаев)."),
    "cuddly": ("Ласкается", "Начать гладить пальцем по экрану: примерно раз из шести, если любовь выше 50%, иначе редко."),
    "eager": ("В предвкушении", "Покормить: зажать нижнюю кнопку на 0.6 с, пока летит печенье. Чаще, если он не сыт."),
    "yummy": ("Смакует", "Покормить (зажать нижнюю кнопку), когда он рад еде, — примерно в половине случаев после «ням»."),
    "triumphant": ("Торжествует", "Проиграть ему в игре с ним (пинг-понг, «Повтори за мной», крестики-нолики против Пипа) и вернуться к нему в течение 10 секунд — примерно в половине случаев."),
    "deflated": ("Сдулся", "Обыграть его в игре с ним (пинг-понг, «Повтори за мной», крестики-нолики против Пипа) и вернуться к нему в течение 10 секунд — примерно в половине случаев, если он не обиделся."),
    "sparkling": ("Сияет чистотой", "Помыть грязного до чистоты: тап нижней кнопки и сразу зажать её, держать, пока не отмоется (примерно в половине случаев)."),
    "groggy": ("Спросонья", "Разбудить спящего: несколько тапов нижней кнопкой или перевернуть экраном вверх после сна экраном вниз (вариант, примерно раз из трёх)."),
    "starstruck": ("Звёзды в глазах", "Побить рекорд в «Шарике» и вернуться к нему. Пройти дневную норму шагов (примерно раз из трёх вместо гордости). Сам, в отличный день."),
    "singing": ("Поёт", "Сам, в хорошем настроении днём. Натанцевался под музыку — иногда после неё ещё поёт."),
    "kiss": ("Чмок", "Гладить по экрану долго (7+ с), когда он тебя любит, — иногда вместо «растроган». Изредка — тап между глаз, когда очень любит."),
    "flirty": ("Кокетничает", "Тап по щеке или под подбородком, когда любовь выше 50% (иногда). Сам."),
    "rainbow": ("Радужный", "Сам, когда всех потребностей больше половины и настроение хорошее. Чаще в день рождения владельца."),
    "peeking": ("Выглядывает", "Ку-ку (на миг лицом вниз и обратно) — иногда. Сам, когда не скучает и доверяет."),
    "zen": ("Дзен", "Долго держать палец на экране, не двигая, — срабатывает случайно."),
    "sweetdream": ("Сладкий сон", "Уснул сытым, чистым и любимым — чаще всего спит так."),
    "melting": ("Растаял", "Гладить по экрану долго (7+ с), когда он тебя любит, — иногда вместо «растроган»."),
    "applause": ("Аплодирует", "Обыграть его в игре с ним и вернуться к нему в течение 10 секунд — примерно в трети случаев, если доверяет и не обижен. Побить рекорд в «Шарике»."),
    "cool": ("Крутой", "Сам, в хорошем настроении. Проиграть ему в игре с ним и вернуться — иногда."),
    "puppy": ("Щенячьи глазки", "Сам, когда проголодался или мало любви. Начать гладить и бросить три раза подряд — иногда вместо «надулся»."),
    "butterfly": ("Бабочка", "Сам, днём (8–20 ч)."),
    "bubbles": ("Пузыри", "Помыть грязного до чистоты — иногда после мытья. Сам, пока мокрый или совсем чистый."),
    "robot": ("Робот", "Сам, когда весело. Чаще, когда давно стоит на зарядке."),
    "kitty": ("Мурлыка", "Гладить по голове (верх экрана) или под подбородком — иногда, чаще когда любит. Сам."),
    "silly": ("Дурачится", "Ткнуть пальцем не в глаз, когда ему весело (иногда). Изредка — тап между глаз. Сам, в хороший день."),
    "snicker": ("Давится смехом", "Тап в бок (иногда). Увернулся от пальца — примерно раз из трёх. Сам, в хорошем настроении."),
    "campfire": ("У костра", "Замёрз (долго дуть на него) — потом садится греться. Сам, вечером и ночью (19–6 ч) или когда включён кабель."),
    "stargazing": ("Звездочёт", "Сам, поздно вечером и ночью (21–5 ч), когда не спит."),
    "zapped": ("Заряд бодрости", "Подключить кабель, когда он бодрый и заряда больше 30%."),
    "sipping": ("Пьёт ток", "Подключить кабель, когда заряда меньше 30% или он устал."),
    "xd": ("Смех 2", "Щекотать: быстро тереть пальцем одно маленькое место — примерно в половине случаев вместо обычного смеха."),
    "rofl": ("Смех 3", "Щекотать дольше 4 секунд — примерно в половине случаев он начинает кататься со смеху."),
    "bouncy": ("Радость 2", "Начать гладить в хорошем настроении — иногда. Покормить, когда он рад, — иногда."),
    "blooming": ("Радость 3", "Гладить долго (7+ с) в хорошем настроении, когда любит, — иногда. Сам в хорошем настроении, весной и летом чаще."),
    "hearteyes": ("Любовь 2", "Гладить долго (7+ с), когда любовь выше 80%, — иногда вместо «растроган»."),
    "fireworks": ("Восторг 2", "Пройти дневную норму шагов — иногда вместо гордости. Побить рекорд в игре и вернуться к нему."),
    "medal": ("Гордость 2", "Пройти дневную норму шагов — примерно в трети случаев вместо гордости. Проиграть ему в игре с ним и вернуться в течение 10 с — иногда."),
    "disco": ("Танец 2", "Включить музыку рядом — примерно каждый третий танец под зеркальным шаром."),
    "popeyed": ("Изумление 2", "Ку-ку (на миг лицом вниз и обратно) — иногда. Громко хлопнуть, когда доверие выше 60%, — иногда он не пугается, а изумляется. Сам."),
    "lightbulb": ("Ага! 2", "Сам: задумался и придумал — по очереди с «Ага!»."),
    "paws": ("Смущение 2", "Тап по щеке — иногда. Погладить по голове (верх экрана), когда он смущается."),
    "hello": ("Привет!", "Взять брелок в руки, когда экран погас, а его не трогали 5+ минут. Подойти после долгой разлуки — иногда. Тап нижней кнопкой, когда он тебя любит, — иногда."),
    "king": ("Король", "Проиграть ему в игре с ним и вернуться в течение 10 с — иногда. Сам, в отличном настроении."),
    "angel": ("Ангелочек", "Помыть грязного до чистоты — иногда. Сразу после того, как он рыгнул или пукнул, — иногда."),
    "imp": ("Проказник", "Увернулся от пальца — иногда (весёлый и доверчивый, он сам уклоняется от пальца, который тянется к глазу). Сам, когда замышляет шалость."),
    "juggler": ("Жонглёр", "Легонько потрясти из стороны в сторону — иногда, чаще когда весело. Сам, когда весело."),
    "balloon": ("Воздушный шарик", "Поднять брелок со стола, когда экран погас, днём (9–20 ч) — иногда. Сам, днём."),
    "icecream": ("Мороженое", "Покормить, когда он рад еде, — иногда вместо «ням» и «смакует», летом чаще."),
    "umbrella": ("Под зонтиком", "Помыть до чистоты — иногда. Сам, пока мокрый после купания. Сам, осенью."),
    "strongman": ("Силач", "На прогулке: пройти половину дневной нормы шагов и достать брелок (поднять или перевернуть экраном вверх) — чаще всего. Сам, на прогулке и после неё."),
    "magician": ("Фокусник", "Ткнуть пальцем, когда он притворяется спящим, — иногда вместо «бу!». Сам, когда весело."),
    "sunrise": ("Доброе утро", "Проснулся утром (6–11 ч) сытым и любимым — чаще всего, раз в день."),
    "sheep": ("Считает овец", "Сам, вечером и ночью (20–6 ч), чаще когда устал."),
    "ninja": ("Ниндзя", "Быстро смахнуть пальцем по экрану — иногда. Увернулся от пальца — иногда."),
    "sprout": ("Росток", "Помыть до чистоты в хорошем настроении — иногда. Сам, весной."),
    "raincloud": ("Грусть 2", "Сам, в плохой день или когда любви меньше 35%."),
    "sobbing": ("Грусть 3", "Уронить или бросить, когда он и так расстроен или любви почти нет, — примерно в половине случаев."),
    "heartbroken": ("Разбитое сердце", "Бросить брелок — иногда. Подойти после долгой разлуки, когда он обижен, — иногда."),
    "fuming": ("Злость 2", "Надоесть тычками до злости — иногда. Дёргать кабель туда-сюда — иногда."),
    "burning": ("Злость 3", "Довести тычками до ярости (верх лестницы злости) — в половине случаев вместо ярости."),
    "sulking": ("Обида 2", "Три раза подряд начать гладить и бросить — иногда. Ревнует к телефону — иногда. Сам, пока обида не прошла."),
    "spooked": ("Страх 2", "Громкий хлопок вечером или ночью (19–6 ч) — иногда. Сам, ночью (22–5 ч), если доверие ниже 40%."),
    "tumbleweed": ("Скука 2", "Сам, когда скучно (мало веселья) — по очереди со вздохом скуки."),
    "lowbatt": ("Усталость 2", "Сам, когда батарея ниже 10% без кабеля или энергия почти на нуле."),
    "hungry": ("Голодные мечты", "Сам, когда голоден (еды меньше 30%)."),
    "knockout": ("Нокаут", "Сильно трясти долго — часто. Сильно встряхнуть или уронить — иногда."),
    "wilted": ("Тоска", "Сам, когда давно без внимания (любви меньше 30%). Сам, вечером в грустный день."),
    "queasy": ("Тошнит", "Перекормить: кормить, когда он уже сыт, — примерно в половине случаев вместо «объелся»."),
    "charged": ("Зарядился", "Вытащить кабель, когда заряд 80% и больше (не сразу после подключения)."),
    "cutoff": ("Не дали допить", "Вытащить кабель, когда заряд от 30 до 79% (не сразу после подключения)."),
    "empty": ("Обесточен", "Вытащить кабель, когда заряд ниже 30% (не сразу после подключения)."),
    "sniffles": ("Простуда", "Сам, когда болен (долго грязный). Замёрз мокрым (дуть на него после купания) — потом простужается."),
    "newtree": ("Ёлочка", "1 января (если часы брелока синхронизированы): при первом включении за день — ёлочка или венок, и иногда в течение дня."),
    "wreath": ("Рождественский венок", "1 января (если часы брелока синхронизированы): при первом включении за день — ёлочка или венок, и иногда в течение дня."),
}


# the filters: good and bad by how he takes the feeling (ui/pip.c mood_vals, hundredths; under +-0.2 neither), rare ones
# by hand (the rare cards of the day and the top of the anger ladder), his own ones by what the "how" says (he does it
# himself, the day's palette, a need or the body running low)
RARE = {"stoned", "pinkeye", "allergic", "frozen", "overheated", "smitten", "hypnotized", "hyper", "zombie", "berserk"}
SELF = re.compile(r"Сам|сам |[Пп]алитр|Редкая карта|засыпает сам|ниже \d|Голоден|Батарея|Энергия|Грязный|Давно не|Мало любви|Любовь ниже|Веселье ниже|Скучно")


def valences():
    with open(os.path.join(ROOT, "ui", "pip.c"), encoding="utf-8") as f:
        src = f.read()
    body = src[src.index("static void mood_vals(void)"):]
    body = body[:body.index("};")]
    return {n: int(v) / 100 for n, v in re.findall(r'\{"(\w+)", (-?\d+)\}', body)}


def moods():
    with open(os.path.join(ROOT, "ui", "moods.h"), encoding="utf-8") as f:
        body = f.read().split("enum {")[1].split("PIP_MOOD_COUNT")[0]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    return [m[4:].lower() for m in re.findall(r"PIP_[A-Z_]+", body)]


# a scene's telling moment, s after the face came (the rest are taken at 2.5 s)
WHEN = {"starstruck": 1.0, "singing": 2.2, "kiss": 1.2, "flirty": 1.15, "peeking": 1.9, "sweetdream": 2.0, "melting": 2.7, "applause": 0.75, "cool": 3.5,
        "butterfly": 3.6, "bubbles": 4.4, "kitty": 3.5, "snicker": 3.4, "stargazing": 2.2, "zapped": 0.6, "sipping": 2.9,
        "xd": 1.05, "rofl": 0.75, "bouncy": 0.55, "blooming": 2.95, "hearteyes": 1.15, "fireworks": 2.5, "medal": 1.25, "disco": 1.25, "popeyed": 1.2, "lightbulb": 3.1,
        "paws": 2.2, "hello": 1.0, "king": 0.35, "angel": 2.75, "imp": 1.5, "juggler": 0.85, "balloon": 1.6, "icecream": 1.0, "umbrella": 1.15, "strongman": 2.75,
        "magician": 3.5, "sunrise": 3.25, "sheep": 1.75, "ninja": 0.75, "sprout": 4.05, "raincloud": 1.35, "sobbing": 2.65, "heartbroken": 2.15, "fuming": 2.75, "burning": 1.35,
        "sulking": 2.75, "spooked": 3.25, "tumbleweed": 1.75, "lowbatt": 0.25, "hungry": 2.35, "knockout": 1.25, "wilted": 1.85, "sniffles": 1.4,
        "charged": 1.55, "cutoff": 1.25, "empty": 3.05, "queasy": 1.4, "newtree": 2.0, "wreath": 2.6}


def shot(i, name):
    """the mood pinned in pipsim (cached in build/moods), as a small WebP in docs/list/img"""
    path = os.path.join(SHOTS, f"{i:02d}_{name}.png")
    if not os.path.exists(path):
        os.makedirs(SHOTS, exist_ok=True)
        subprocess.run([SIM, "--out", path, "--secs", str(WHEN.get(name, 2.45) + 0.05), "--events", f"0:Kvary=0 0:Klife_gap=0 0:s0 0.05:n{name}"], check=True, capture_output=True)
    im = Image.open(path).convert("RGB").resize((240, 240), Image.LANCZOS)
    os.makedirs(os.path.join(LIST, "img"), exist_ok=True)
    im.save(os.path.join(LIST, "img", name + ".webp"), "WEBP", quality=82, method=6)
    return f"img/{name}.webp"


# the scenes' own periods, s (ui/pip.c show_face); two of them are filmed
LOOP = {"starstruck": 3.36, "singing": 4.48, "kiss": 2.8, "flirty": 2.0, "rainbow": 4.5, "peeking": 4.6, "zen": 4.8, "sweetdream": 3.2, "melting": 4.4, "applause": 3.2,
        "cool": 2.6, "puppy": 3.0, "butterfly": 6.0, "bubbles": 4.8, "robot": 4.0, "kitty": 3.6, "silly": 1.6, "snicker": 4.4, "campfire": 4.0, "stargazing": 5.2,
        "zapped": 3.6, "sipping": 5.0,
        "xd": 3.2, "rofl": 2.0, "bouncy": 2.4, "blooming": 5.0, "hearteyes": 3.6, "fireworks": 4.5, "medal": 4.0, "disco": 4.0, "popeyed": 3.8, "lightbulb": 4.6,
        "paws": 5.2, "hello": 3.0, "king": 5.0, "angel": 5.0, "imp": 3.4, "juggler": 1.6, "balloon": 5.4, "icecream": 6.0, "umbrella": 3.6, "strongman": 5.0,
        "magician": 5.2, "sunrise": 6.0, "sheep": 6.0, "ninja": 4.2, "sprout": 6.0, "raincloud": 4.4, "sobbing": 4.8, "heartbroken": 5.0, "fuming": 4.0, "burning": 3.0,
        "sulking": 5.0, "spooked": 6.0, "tumbleweed": 6.0, "lowbatt": 4.2, "hungry": 5.6, "knockout": 4.0, "wilted": 6.4, "sniffles": 4.6,
        "charged": 4.0, "cutoff": 4.4, "empty": 4.6, "queasy": 4.2, "newtree": 4.8, "wreath": 5.0}
FPS, SIDE = 25, 240


def film(i, name):
    """a short loop of the mood as a small muted MP4 in docs/list/film"""
    path, raw = os.path.join(LIST, "film", name + ".mp4"), os.path.join(SHOTS, f"{i:02d}_{name}.film.mp4")
    if os.path.exists(path) and os.path.exists(raw):
        return f"film/{name}.mp4"
    subprocess.run([SIM, "--out", raw, "--secs", "14", "--fps", str(FPS), "--events", f"0:Kvary=0 0:Klife_gap=0 0:s0 0.05:n{name}"], check=True, capture_output=True)
    px = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", raw, "-vf", "scale=48:48", "-f", "rawvideo", "-pix_fmt", "gray", "-"], check=True, capture_output=True).stdout
    f = np.frombuffer(px, np.uint8).reshape(-1, 48, 48).astype(np.int16)
    if name in LOOP:
        a = round((0.05 + LOOP[name] * (1 if LOOP[name] < 4 else 1)) * FPS)
        b = a + round(LOOP[name] * FPS)
    else:  # where it comes back to itself, a moment before and after too (the motion matches, not just the place)
        best = None
        for p in range(int(1.5 * FPS), int(7 * FPS), 1):
            for q in range(p + 3 * FPS, min(p + 5 * FPS, len(f) - 3)):
                d = sum(np.abs(f[p + k] - f[q + k]).mean() for k in (-1, 0, 1))
                if best is None or d < best[0]:
                    best = (d, p, q)
        a, b = best[1], best[2]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-ss", f"{a / FPS:.3f}", "-t", f"{(b - a) / FPS:.3f}", "-i", raw, "-an", "-vf", f"scale={SIDE}:{SIDE}:flags=lanczos,format=yuv420p",
                    "-c:v", "libx264", "-profile:v", "main", "-crf", "25", "-preset", "veryslow", "-g", "250", "-movflags", "+faststart", path], check=True)
    return f"film/{name}.mp4"


def sounds():
    """every feeling's sounds (pipsim --sounds) as small MP3s in docs/list/snd: {mood: [key...]}"""
    raw, out = os.path.join(SHOTS, "snd"), os.path.join(LIST, "snd")
    os.makedirs(raw, exist_ok=True), os.makedirs(out, exist_ok=True)
    lines = subprocess.run([SIM, "--sounds", raw], check=True, capture_output=True, text=True, encoding="utf-8").stdout.split("\n")
    per = {l.split()[0]: l.split()[1:] for l in lines if l.strip()}
    keys = sorted({k for v in per.values() for k in v})
    def one(k):
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "s16le", "-ar", "16000", "-ac", "1", "-i", os.path.join(raw, k + ".raw"), "-af", "adelay=20,apad=pad_dur=0.08",
                        "-ar", "32000", "-b:a", "48k", os.path.join(out, k + ".mp3")], check=True)
    with ThreadPoolExecutor(8) as ex:
        list(ex.map(one, keys))
    for f in os.listdir(out):
        if f[:-4] not in keys:
            os.remove(os.path.join(out, f))
    return per


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


PAGE = """<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Пип — эмоции</title>
<link rel="icon" href="../favicon.svg?v=curious" type="image/svg+xml">
<link rel="apple-touch-icon" href="../apple-touch-icon.png?v=curious">
<meta name="theme-color" content="#060607">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Manrope:wght@400;500;600;700;800&family=JetBrains+Mono:wght@500;600&display=swap" rel="stylesheet">
<link rel="stylesheet" href="../pip.css">
<style>
.hero{background:linear-gradient(180deg,var(--card),var(--bg2));border:1px solid var(--line);border-radius:28px;padding:26px 30px;margin-bottom:18px}
.hero h1{font:800 30px/1.15 var(--sans);letter-spacing:-.6px;margin:0 0 8px}
.hero p{margin:0;color:var(--dim);max-width:78ch}
.hero p+p{margin-top:8px}
.bar{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:12px 0;background:linear-gradient(var(--bg) 80%,transparent)}
.bar input{flex:1 1 280px}
.seg{display:inline-flex;gap:4px;padding:4px;background:var(--card);border:1px solid var(--line);border-radius:14px;flex-wrap:wrap}
.seg button{height:34px;border:0;background:transparent;color:var(--dim);border-radius:10px;padding:0 13px}
.seg button:hover{color:var(--text)}
.seg button.on{background:var(--card2);color:var(--text);box-shadow:0 1px 0 rgba(255,255,255,.06) inset,0 4px 12px -6px #000}
.seg button span{color:var(--faint);font-weight:600;margin-left:6px}
.list{display:grid;gap:10px}
.mood{display:grid;grid-template-columns:44px 132px minmax(150px,220px) minmax(0,1fr);gap:20px;align-items:center;background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:12px 20px 12px 14px}
.mood:target{border-color:var(--acc)}
.num{font:600 15px var(--mono);color:var(--faint);text-align:center}
.face{width:132px;height:132px;border-radius:50%;overflow:hidden;background:#000;position:relative;box-shadow:0 0 0 5px #1b1b20,0 0 0 6px var(--line2);transition:box-shadow .25s}
.face img,.face video{position:absolute;inset:0;width:100%;height:100%;object-fit:cover;display:block}
.face video{opacity:0;transition:opacity .2s}
.face video.on{opacity:1}
.mood.talks .face{box-shadow:0 0 0 5px #1b1b20,0 0 0 7px var(--acc),0 0 26px -4px rgba(255,106,43,.55)}
.snd{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}
.snd button{width:40px;height:40px;padding:0;border-radius:50%;background:var(--card2);color:var(--acc2);position:relative}
.snd button:hover{border-color:var(--acc);background:var(--accbg)}
.snd svg{width:18px;height:18px;fill:currentColor;display:block}
.snd .bars{display:none;align-items:center;gap:2.5px;height:18px}
.snd .bars i{width:3px;height:6px;border-radius:2px;background:currentColor;animation:bar .7s ease-in-out infinite}
.snd .bars i:nth-child(2){animation-delay:-.45s}.snd .bars i:nth-child(3){animation-delay:-.2s}
@keyframes bar{50%{height:17px}}
.snd button.on{background:linear-gradient(180deg,var(--acc2),var(--acc));border-color:transparent;color:#140800}
.snd button.on svg{display:none}.snd button.on .bars{display:flex}
.name b{display:block;font:700 17px/1.25 var(--sans)}
.name code{display:inline-block;margin-top:4px;font-size:12px;color:var(--dim)}
.tags{display:flex;flex-wrap:wrap;gap:5px;margin-top:8px}
.tag{font:700 11px/1 var(--sans);padding:5px 8px;border-radius:999px;border:1px solid var(--line2);color:var(--dim)}
.tag.good{color:#7ee2a0;border-color:rgba(48,209,88,.35);background:rgba(48,209,88,.08)}
.tag.bad{color:#ff9a92;border-color:rgba(255,69,58,.35);background:rgba(255,69,58,.08)}
.tag.self{color:#9cc9ff;border-color:rgba(90,169,255,.35);background:rgba(90,169,255,.08)}
.tag.rare{color:#ffd88a;border-color:rgba(255,214,10,.35);background:rgba(255,214,10,.08)}
.how{margin:0;color:#d9d6cf;font-size:14.5px}
.none{text-align:center;color:var(--dim);padding:40px 0}
@media (max-width:760px){.mood{grid-template-columns:92px minmax(0,1fr);gap:12px 18px;padding:16px}.num{display:none}.face{width:92px;height:92px}.txt{grid-column:1/-1}.snd button{width:44px;height:44px}}
</style>
</head>
<body>
<div class="wrap">
<header class="top">
  <a class="brand" href="../"><span class="logo"><i></i><i></i></span><span><b>Пип</b><small>все эмоции</small></span></a>
  <nav class="nav"><a href="../">Панель</a><a href="../sim/">Симулятор</a><a href="./" class="on">Эмоции</a><a href="../debug/">Отладка</a></nav>
</header>
<section class="hero">
  <h1>Эмоции Пипа</h1>
  <p>Здесь собраны все эмоции Пипа: как выглядят, как звучат и что сделать, чтобы их увидеть.
    Номер слева — постоянный номер эмоции в системе.</p>
  <p>«Палитра дня» — фоновое настроение по тому, как у Пипа дела (отлично, хорошо, так себе, плохо).</p>
</section>
<div class="bar">
  <input id="q" type="search" placeholder="Поиск: название, номер или действие — например, «погладить»" autocomplete="off">
  <div class="seg" role="group" aria-label="Фильтр">
    <button data-f="" class="on">Все<span>@TOTAL@</span></button><button data-f="good">Хорошие<span>@good@</span></button>
    <button data-f="bad">Плохие<span>@bad@</span></button><button data-f="self">Автоматические<span>@self@</span></button>
    <button data-f="rare">Редкие<span>@rare@</span></button>
  </div>
</div>
<main class="list" id="list">
@ROWS@
</main>
<p class="none" id="none" hidden>Ничего не нашлось — попробуй другое слово или фильтр «Все»</p>
</div>
<script>
const rows = [...document.querySelectorAll(".mood")], q = document.getElementById("q"), btns = [...document.querySelectorAll(".seg button")];
let f = "";
function apply() {
  const words = q.value.toLowerCase().split(/\\s+/).filter(Boolean);
  let n = 0;
  for (const r of rows) {
    const text = r.textContent.toLowerCase(), ok = (!f || r.dataset.tags.split(" ").includes(f)) && words.every(w => text.includes(w));
    r.hidden = !ok, n += ok;
  }
  document.getElementById("none").hidden = n > 0;
}
q.addEventListener("input", apply);
for (const b of btns) b.onclick = () => { f = b.dataset.f; btns.forEach(x => x.classList.toggle("on", x === b)); apply(); };
apply();
/* the films: loaded and played only while seen; the picture stays under until the first frame is there */
const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
const go = v => { if (!v.src) v.src = v.dataset.src; v.play().then(() => v.classList.add("on")).catch(() => {}); };
if (still) document.querySelectorAll(".face").forEach(f => { const v = f.querySelector("video"); f.onmouseenter = () => go(v); f.onmouseleave = () => v.pause(); });
else {
  const eye = new IntersectionObserver(es => es.forEach(e => e.isIntersecting ? go(e.target) : e.target.pause()), { rootMargin: "120px 0px" });
  document.querySelectorAll(".face video").forEach(v => eye.observe(v));
}
/* the sounds: one at a time */
let snd = null, was = null;
const hush = () => { if (snd) snd.pause(), snd = null; if (was) was.classList.remove("on"), was.closest(".mood").classList.remove("talks"), was = null; };
document.getElementById("list").addEventListener("click", e => {
  const b = e.target.closest("button[data-s]");
  if (!b) return;
  const again = b === was;
  hush();
  if (again) return;
  snd = new Audio("snd/" + b.dataset.s + ".mp3"), was = b;
  b.classList.add("on"), b.closest(".mood").classList.add("talks");
  snd.onended = snd.onerror = hush;
  snd.play().catch(hush);
});
</script>
</body>
</html>
"""


def main():
    names = moods()
    missing = [n for n in names if n not in HOW]
    if missing:
        sys.exit("no description for: " + ", ".join(missing))
    val = valences()
    snd = sounds()
    with ThreadPoolExecutor(6) as ex:
        list(ex.map(lambda t: film(*t), enumerate(names)))
    def play(keys):
        btn = "".join(f'<button type="button" data-s="{k}" aria-label="Звук {j}"><svg viewBox="0 0 24 24"><path d="M8 5.2v13.6a1 1 0 0 0 1.5.86l11-6.8a1 1 0 0 0 0-1.72l-11-6.8A1 1 0 0 0 8 5.2z"/></svg>'
                      f'<span class="bars"><i></i><i></i><i></i></span></button>' for j, k in enumerate(keys, 1))
        return f'<div class="snd">{btn}</div>' if keys else ""
    rows, js = [], []
    count = {"good": 0, "bad": 0, "self": 0, "rare": 0}
    for i, n in enumerate(names):
        ru, how = HOW[n]
        v = val.get(n, 0)
        tags = [t for t, on in (("good", v >= 0.2), ("bad", v <= -0.2), ("self", bool(SELF.search(how))), ("rare", n in RARE)) if on]
        for t in tags:
            count[t] += 1
        badges = "".join(f'<span class="tag {t}">{lbl}</span>' for t, lbl in (("good", "хорошая"), ("bad", "плохая"), ("self", "авто"), ("rare", "редкая")) if t in tags)
        rows.append(f'<article class="mood" data-tags="{" ".join(tags)}" id="{n}"><div class="num">{i}</div>'
                    f'<div class="face"><img src="{shot(i, n)}" alt="{esc(ru)}" loading="lazy" width="132" height="132"><video data-src="film/{n}.mp4" muted loop playsinline preload="none" aria-hidden="true"></video></div>'
                    f'<div class="name"><b>{esc(ru)}</b><code>{n}</code><div class="tags">{badges}</div></div>'
                    f'<div class="txt"><p class="how">{esc(how)}</p>{play(snd.get(n, []))}</div></article>')
        js.append(f'  {n}: [{i}, {json.dumps(ru, ensure_ascii=False)}, {json.dumps(how, ensure_ascii=False)}]')
    with open(os.path.join(LIST, "names.js"), "w", encoding="utf-8", newline="\n") as f:
        f.write("/* made by tools/emotions_table.py: every mood (ui/moods.h) - its number, its Russian name, how to get it.\n"
                "   The debug page (docs/debug) shows these instead of the firmware's ids. */\nwindow.PIP_MOODS = {\n" + ",\n".join(js) + "\n};\n")
    html = PAGE.replace("@ROWS@", "\n".join(rows)).replace("@TOTAL@", str(len(names)))
    for k, v in count.items():
        html = html.replace(f"@{k}@", str(v))
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(html)
    print(OUT, len(names), "moods", os.path.getsize(OUT) // 1024, "KB")


if __name__ == "__main__":
    main()
