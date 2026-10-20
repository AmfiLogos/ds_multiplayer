#!/usr/bin/env python3
"""Lane pvprules: translations of the PvP Rules box strings into the 9 languages of lang/. Adds missing keys only.
   python3 ext/pvprules/tr_pvprules.py  (then: python3 tools/i18n_tool.py check lang)"""
import json, os
W = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
L = ['de', 'es', 'fr', 'ja', 'ko', 'pt-BR', 'ru', 'zh-Hans', 'zh-Hant']
T = {
 'Boss room': ['Bossraum', 'Sala del jefe', 'Salle du boss', 'ボス部屋', '보스 방', 'Sala do chefe', 'Комната босса', '首领房间', '首領房間'],
 'Custom': ['Eigene', 'Personalizado', 'Personnalisé', 'カスタム', '사용자 지정', 'Personalizado', 'Свои', '自定义', '自訂'],
 'Delete preset': ['Vorlage löschen', 'Borrar preajuste', 'Supprimer le préréglage', 'プリセットを削除', '프리셋 삭제', 'Excluir predefinição', 'Удалить набор', '删除预设', '刪除預設'],
 'Deletes the preset shown. The rules stay as they are.': [
   'Löscht die angezeigte Vorlage. Die Regeln bleiben, wie sie sind.', 'Borra el preajuste mostrado. Las reglas se quedan como están.',
   'Supprime le préréglage affiché. Les règles restent telles quelles.', '表示中のプリセットを削除します。ルールはそのままです。',
   '표시된 프리셋을 삭제합니다. 규칙은 그대로입니다.', 'Exclui a predefinição mostrada. As regras ficam como estão.',
   'Удаляет показанный набор. Правила остаются как есть.', '删除显示的预设。规则保持不变。', '刪除顯示的預設。規則保持不變。'],
 'Dungeon %d: %s': ['Dungeon %d: %s', 'Mazmorra %d: %s', 'Donjon %d : %s', 'ダンジョン%d：%s', '던전 %d: %s', 'Masmorra %d: %s', 'Подземелье %d: %s', '地下城 %d：%s', '地下城 %d：%s'],
 'Enemy health bars': ['Gegner-Lebensbalken', 'Barras de vida enemigas', 'Barres de vie ennemies', '敵のHPバー', '적 체력 바', 'Barras de vida inimigas', 'Полоски здоровья врагов', '敌方血条', '敵方血條'],
 'Enemy portraits': ['Gegner-Porträts', 'Retratos enemigos', 'Portraits ennemis', '敵の肖像', '적 초상화', 'Retratos inimigos', 'Портреты врагов', '敌方头像', '敵方頭像'],
 'Enemy status': ['Gegner-Status', 'Estado enemigo', 'État des ennemis', '敵の状態', '적 상태', 'Estado inimigo', 'Состояние врагов', '敌方状态', '敵方狀態'],
 'Enemy view range': ['Gegner-Sichtweite', 'Visión enemiga', 'Vue ennemie', '敵の視界範囲', '적 시야 범위', 'Visão inimiga', 'Обзор врагов', '敌方视野范围', '敵方視野範圍'],
 'Enemy vision': ['Gegner sichtbar', 'Ver enemigos', 'Voir les ennemis', '敵の可視化', '적 보기', 'Ver inimigos', 'Видеть врагов', '可见敌人', '可見敵人'],
 'Enemy windows': ['Gegner-Fenster', 'Ventanas enemigas', 'Fenêtres ennemies', '敵のウィンドウ', '적 창', 'Janelas inimigas', 'Окна врагов', '敌方窗口', '敵方視窗'],
 'Floor': ['Ebene', 'Piso', 'Étage', '階層', '층', 'Andar', 'Этаж', '楼层', '樓層'],
 'Floor %d': ['Ebene %d', 'Piso %d', 'Étage %d', '%d階', '%d층', 'Andar %d', 'Этаж %d', '第 %d 层', '第 %d 層'],
 'Hidden': ['Verborgen', 'Oculto', 'Masqué', '非表示', '숨김', 'Oculto', 'Скрыто', '隐藏', '隱藏'],
 'In the lobby each player brings units; Start sends the teams to the arena floor. The match begins once everyone has arrived.': [
   'In der Lobby bringt jeder Spieler Einheiten mit; Start schickt die Teams auf die Arena-Ebene. Das Match beginnt, sobald alle angekommen sind.',
   'En la sala cada jugador trae unidades; Iniciar envía a los equipos al piso de la arena. La partida empieza cuando todos han llegado.',
   "Dans le salon, chaque joueur amène des unités ; Démarrer envoie les équipes à l'étage de l'arène. Le match commence quand tout le monde est arrivé.",
   'ロビーで各プレイヤーがユニットを連れてきます。開始でチームがアリーナの階へ移動し、全員が着いたら試合が始まります。',
   '로비에서 각 플레이어가 유닛을 데려옵니다. 시작하면 팀이 아레나 층으로 이동하고, 모두 도착하면 경기가 시작됩니다.',
   'No saguão cada jogador traz unidades; Iniciar envia as equipes ao andar da arena. A partida começa quando todos chegarem.',
   'В лобби каждый игрок приводит юнитов; «Старт» отправляет команды на этаж арены. Матч начнётся, когда все прибудут.',
   '在大厅中每位玩家带来单位；开始后队伍前往竞技场楼层。所有人到达后比赛开始。',
   '在大廳中每位玩家帶來單位；開始後隊伍前往競技場樓層。所有人到達後比賽開始。'],
 'Map': ['Karte', 'Mapa', 'Carte', 'マップ', '맵', 'Mapa', 'Карта', '地图', '地圖'],
 'No presets yet': ['Noch keine Vorlagen', 'Aún no hay preajustes', 'Aucun préréglage', 'プリセットなし', '프리셋 없음', 'Nenhuma predefinição', 'Наборов пока нет', '暂无预设', '尚無預設'],
 'Preset': ['Vorlage', 'Preajuste', 'Préréglage', 'プリセット', '프리셋', 'Predefinição', 'Набор', '预设', '預設'],
 'Preset "%s" applied.': ['Vorlage „%s“ angewendet.', 'Preajuste «%s» aplicado.', 'Préréglage « %s » appliqué.', 'プリセット「%s」を適用しました。', '프리셋 "%s"을(를) 적용했습니다.',
                          'Predefinição "%s" aplicada.', 'Набор «%s» применён.', '已应用预设“%s”。', '已套用預設「%s」。'],
 'Preset "%s" deleted. The rules stay as they are.': ['Vorlage „%s“ gelöscht. Die Regeln bleiben, wie sie sind.', 'Preajuste «%s» borrado. Las reglas se quedan como están.',
                          'Préréglage « %s » supprimé. Les règles restent telles quelles.', 'プリセット「%s」を削除しました。ルールはそのままです。',
                          '프리셋 "%s"을(를) 삭제했습니다. 규칙은 그대로입니다.', 'Predefinição "%s" excluída. As regras ficam como estão.',
                          'Набор «%s» удалён. Правила остаются как есть.', '已删除预设“%s”。规则保持不变。', '已刪除預設「%s」。規則保持不變。'],
 'Preset "%s" replaced.': ['Vorlage „%s“ ersetzt.', 'Preajuste «%s» reemplazado.', 'Préréglage « %s » remplacé.', 'プリセット「%s」を上書きしました。', '프리셋 "%s"을(를) 덮어썼습니다.',
                           'Predefinição "%s" substituída.', 'Набор «%s» заменён.', '已替换预设“%s”。', '已取代預設「%s」。'],
 'Preset "%s" saved.': ['Vorlage „%s“ gespeichert.', 'Preajuste «%s» guardado.', 'Préréglage « %s » enregistré.', 'プリセット「%s」を保存しました。', '프리셋 "%s"을(를) 저장했습니다.',
                        'Predefinição "%s" salva.', 'Набор «%s» сохранён.', '已保存预设“%s”。', '已儲存預設「%s」。'],
 'Preset name': ['Name der Vorlage', 'Nombre del preajuste', 'Nom du préréglage', 'プリセット名', '프리셋 이름', 'Nome da predefinição', 'Название набора', '预设名称', '預設名稱'],
 'Random floor': ['Zufällige Ebene', 'Piso aleatorio', 'Étage aléatoire', 'ランダムな階', '무작위 층', 'Andar aleatório', 'Случайный этаж', '随机楼层', '隨機樓層'],
 'Random map': ['Zufällige Karte', 'Mapa aleatorio', 'Carte aléatoire', 'ランダムマップ', '무작위 맵', 'Mapa aleatório', 'Случайная карта', '随机地图', '隨機地圖'],
 'Save as preset...': ['Als Vorlage speichern...', 'Guardar como preajuste...', 'Enregistrer comme préréglage...', 'プリセットとして保存...', '프리셋으로 저장...',
                       'Salvar como predefinição...', 'Сохранить как набор...', '另存为预设...', '另存為預設...'],
 'Saves all the rules shown under a name you type, on this computer only. The same name replaces that preset.': [
   'Speichert alle angezeigten Regeln unter einem Namen, den du eingibst, nur auf diesem Computer. Derselbe Name ersetzt diese Vorlage.',
   'Guarda todas las reglas mostradas con el nombre que escribas, solo en este ordenador. El mismo nombre reemplaza ese preajuste.',
   "Enregistre toutes les règles affichées sous le nom que vous tapez, sur cet ordinateur seulement. Le même nom remplace ce préréglage.",
   '表示中のルールを入力した名前で保存します（このコンピューターのみ）。同じ名前ならそのプリセットを上書きします。',
   '표시된 모든 규칙을 입력한 이름으로 이 컴퓨터에만 저장합니다. 같은 이름이면 그 프리셋을 덮어씁니다.',
   'Salva todas as regras mostradas com o nome que você digitar, só neste computador. O mesmo nome substitui essa predefinição.',
   'Сохраняет все показанные правила под введённым названием, только на этом компьютере. То же название заменяет этот набор.',
   '以你输入的名称保存显示的全部规则，仅限此电脑。同名会替换该预设。', '以你輸入的名稱儲存顯示的全部規則，僅限此電腦。同名會取代該預設。'],
 'Shown': ['Sichtbar', 'Visible', 'Affiché', '表示', '표시', 'Visível', 'Видно', '显示', '顯示'],
 'Shown: clicking an enemy unit opens its full unit window.\n\nHidden: clicking an enemy unit shows only what you see of a monster.': [
   'Sichtbar: Ein Klick auf eine gegnerische Einheit öffnet ihr volles Einheitenfenster.\n\nVerborgen: Ein Klick auf eine gegnerische Einheit zeigt nur, was man auch von einem Monster sieht.',
   'Visible: al hacer clic en una unidad enemiga se abre su ventana completa.\n\nOculto: al hacer clic en una unidad enemiga solo ves lo que verías de un monstruo.',
   "Affiché : cliquer sur une unité ennemie ouvre sa fenêtre d'unité complète.\n\nMasqué : cliquer sur une unité ennemie ne montre que ce qu'on voit d'un monstre.",
   '表示：敵ユニットをクリックすると全ユニット情報のウィンドウが開きます。\n\n非表示：敵ユニットをクリックしても、モンスターと同じ情報しか見えません。',
   '표시: 적 유닛을 클릭하면 전체 유닛 창이 열립니다.\n\n숨김: 적 유닛을 클릭해도 몬스터처럼 보이는 정보만 나옵니다.',
   'Visível: clicar numa unidade inimiga abre a janela completa dela.\n\nOculto: clicar numa unidade inimiga mostra só o que você veria de um monstro.',
   'Видно: щелчок по вражескому юниту открывает его полное окно.\n\nСкрыто: щелчок по вражескому юниту показывает только то, что видно у монстра.',
   '显示：点击敌方单位会打开其完整单位窗口。\n\n隐藏：点击敌方单位只显示与怪物相同的信息。',
   '顯示：點擊敵方單位會開啟其完整單位視窗。\n\n隱藏：點擊敵方單位只顯示與怪物相同的資訊。'],
 'Shown: enemy units are in your portrait bar.\n\nHidden: your portrait bar shows only your own team.': [
   'Sichtbar: Gegnerische Einheiten sind in deiner Porträtleiste.\n\nVerborgen: Deine Porträtleiste zeigt nur dein eigenes Team.',
   'Visible: las unidades enemigas aparecen en tu barra de retratos.\n\nOculto: tu barra de retratos muestra solo tu equipo.',
   'Affiché : les unités ennemies sont dans votre barre de portraits.\n\nMasqué : votre barre de portraits ne montre que votre équipe.',
   '表示：敵ユニットが肖像バーに出ます。\n\n非表示：肖像バーには自分のチームだけが出ます。',
   '표시: 적 유닛이 초상화 바에 나옵니다.\n\n숨김: 초상화 바에는 내 팀만 나옵니다.',
   'Visível: as unidades inimigas aparecem na sua barra de retratos.\n\nOculto: sua barra de retratos mostra só a sua equipe.',
   'Видно: вражеские юниты есть на вашей панели портретов.\n\nСкрыто: на панели портретов только ваша команда.',
   '显示：敌方单位出现在你的头像栏中。\n\n隐藏：头像栏只显示你自己的队伍。', '顯示：敵方單位出現在你的頭像欄中。\n\n隱藏：頭像欄只顯示你自己的隊伍。'],
 "Shown: everyone sees the enemy team's units everywhere on the map.\n\nHidden: you see enemy units only where your own units can see.": [
   'Sichtbar: Alle sehen die Einheiten des gegnerischen Teams überall auf der Karte.\n\nVerborgen: Gegnerische Einheiten siehst du nur dort, wo deine eigenen Einheiten sehen.',
   'Visible: todos ven las unidades del equipo enemigo en todo el mapa.\n\nOculto: solo ves unidades enemigas donde tus unidades pueden ver.',
   "Affiché : tout le monde voit les unités de l'équipe adverse partout sur la carte.\n\nMasqué : vous ne voyez les unités ennemies que là où vos unités voient.",
   '表示：全員がマップ全体で敵チームのユニットを見られます。\n\n非表示：敵ユニットは自分のユニットが見える場所でだけ見えます。',
   '표시: 모두가 맵 전체에서 적 팀의 유닛을 봅니다.\n\n숨김: 적 유닛은 내 유닛이 볼 수 있는 곳에서만 보입니다.',
   'Visível: todos veem as unidades da equipe inimiga em todo o mapa.\n\nOculto: você só vê unidades inimigas onde suas unidades enxergam.',
   'Видно: все видят юнитов вражеской команды по всей карте.\n\nСкрыто: вражеских юнитов видно только там, где их видят ваши юниты.',
   '显示：所有人都能在整张地图上看到敌方队伍的单位。\n\n隐藏：只有你的单位能看到的地方才显示敌方单位。',
   '顯示：所有人都能在整張地圖上看到敵方隊伍的單位。\n\n隱藏：只有你的單位能看到的地方才顯示敵方單位。'],
 'Shown: health bars above enemy units.\n\nHidden: no health bars on enemy units.': [
   'Sichtbar: Lebensbalken über gegnerischen Einheiten.\n\nVerborgen: keine Lebensbalken bei gegnerischen Einheiten.',
   'Visible: barras de vida sobre las unidades enemigas.\n\nOculto: sin barras de vida en las unidades enemigas.',
   'Affiché : barres de vie au-dessus des unités ennemies.\n\nMasqué : pas de barre de vie sur les unités ennemies.',
   '表示：敵ユニットの上にHPバーが出ます。\n\n非表示：敵ユニットにHPバーは出ません。',
   '표시: 적 유닛 위에 체력 바가 나옵니다.\n\n숨김: 적 유닛에 체력 바가 없습니다.',
   'Visível: barras de vida acima das unidades inimigas.\n\nOculto: sem barras de vida nas unidades inimigas.',
   'Видно: полоски здоровья над вражескими юнитами.\n\nСкрыто: у вражеских юнитов нет полосок здоровья.',
   '显示：敌方单位头上显示血条。\n\n隐藏：敌方单位没有血条。', '顯示：敵方單位頭上顯示血條。\n\n隱藏：敵方單位沒有血條。'],
 'Shown: status, mood and work icons above enemy units.\n\nHidden: none of these icons on enemy units.': [
   'Sichtbar: Status-, Stimmungs- und Arbeitssymbole über gegnerischen Einheiten.\n\nVerborgen: keines dieser Symbole bei gegnerischen Einheiten.',
   'Visible: iconos de estado, ánimo y trabajo sobre las unidades enemigas.\n\nOculto: ninguno de estos iconos en las unidades enemigas.',
   "Affiché : icônes d'état, d'humeur et de travail au-dessus des unités ennemies.\n\nMasqué : aucune de ces icônes sur les unités ennemies.",
   '表示：敵ユニットの上に状態・気分・作業アイコンが出ます。\n\n非表示：敵ユニットにこれらのアイコンは出ません。',
   '표시: 적 유닛 위에 상태, 기분, 작업 아이콘이 나옵니다.\n\n숨김: 적 유닛에 이 아이콘들이 없습니다.',
   'Visível: ícones de estado, humor e trabalho acima das unidades inimigas.\n\nOculto: nenhum desses ícones nas unidades inimigas.',
   'Видно: значки состояния, настроения и работы над вражескими юнитами.\n\nСкрыто: у вражеских юнитов нет этих значков.',
   '显示：敌方单位头上显示状态、心情和工作图标。\n\n隐藏：敌方单位不显示这些图标。', '顯示：敵方單位頭上顯示狀態、心情和工作圖示。\n\n隱藏：敵方單位不顯示這些圖示。'],
 'Shown: the area the enemy team can see is lit on the map. Their units stay hidden.\n\nHidden: you do not see where the enemy can see.': [
   'Sichtbar: Der Bereich, den das gegnerische Team sieht, ist auf der Karte hell. Seine Einheiten bleiben verborgen.\n\nVerborgen: Du siehst nicht, wohin der Gegner sieht.',
   'Visible: la zona que ve el equipo enemigo se ilumina en el mapa. Sus unidades siguen ocultas.\n\nOculto: no ves dónde puede ver el enemigo.',
   "Affiché : la zone que voit l'équipe adverse est éclairée sur la carte. Ses unités restent cachées.\n\nMasqué : vous ne voyez pas où l'ennemi voit.",
   '表示：敵チームの見えている範囲がマップ上で明るくなります。敵ユニットは隠れたままです。\n\n非表示：敵がどこを見ているかは分かりません。',
   '표시: 적 팀이 볼 수 있는 영역이 맵에서 밝게 보입니다. 적 유닛은 숨겨진 채로 있습니다.\n\n숨김: 적이 어디를 보는지 알 수 없습니다.',
   'Visível: a área que a equipe inimiga vê fica iluminada no mapa. As unidades dela continuam ocultas.\n\nOculto: você não vê onde o inimigo enxerga.',
   'Видно: область, которую видит вражеская команда, подсвечена на карте. Её юниты остаются скрытыми.\n\nСкрыто: вы не видите, куда смотрит враг.',
   '显示：敌方队伍能看到的区域在地图上被照亮，但其单位仍然隐藏。\n\n隐藏：你看不到敌人能看到哪里。',
   '顯示：敵方隊伍能看到的區域在地圖上被照亮，但其單位仍然隱藏。\n\n隱藏：你看不到敵人能看到哪裡。'],
 "This settlement: the PvP world's map; teams start at its two ends.\n\nRandom map: a random dungeon floor, random start points for each team.\n\nDungeon 1 or 2: a floor of that dungeon, made by the game from a new seed. The same map for every player, with no monsters, no items and no portal.": [
   'Diese Siedlung: die Karte der PvP-Welt; die Teams starten an ihren beiden Enden.\n\nZufällige Karte: eine zufällige Dungeon-Ebene, zufällige Startpunkte für jedes Team.\n\nDungeon 1 oder 2: eine Ebene dieses Dungeons, vom Spiel aus einem neuen Startwert erzeugt. Dieselbe Karte für alle Spieler, ohne Monster, ohne Gegenstände und ohne Portal.',
   'Este asentamiento: el mapa del mundo PvP; los equipos empiezan en sus dos extremos.\n\nMapa aleatorio: un piso de mazmorra aleatorio, puntos de inicio aleatorios para cada equipo.\n\nMazmorra 1 o 2: un piso de esa mazmorra, creado por el juego con una semilla nueva. El mismo mapa para todos, sin monstruos, sin objetos y sin portal.',
   "Cette colonie : la carte du monde PvP ; les équipes partent de ses deux extrémités.\n\nCarte aléatoire : un étage de donjon au hasard, des points de départ au hasard pour chaque équipe.\n\nDonjon 1 ou 2 : un étage de ce donjon, créé par le jeu à partir d'une nouvelle graine. La même carte pour tous, sans monstres, sans objets et sans portail.",
   'この入植地：PvPワールドのマップ。チームは両端から始まります。\n\nランダムマップ：ランダムなダンジョンの階、各チームの開始地点もランダムです。\n\nダンジョン1または2：そのダンジョンの階を、ゲームが新しいシードで作ります。全員が同じマップで、モンスター・アイテム・ポータルはありません。',
   '이 정착지: PvP 월드의 맵. 팀은 맵의 양 끝에서 시작합니다.\n\n무작위 맵: 무작위 던전 층, 팀마다 무작위 시작 지점.\n\n던전 1 또는 2: 게임이 새 시드로 만든 그 던전의 층. 모두에게 같은 맵이며 몬스터, 아이템, 포털이 없습니다.',
   'Este assentamento: o mapa do mundo PvP; as equipes começam nas duas pontas.\n\nMapa aleatório: um andar de masmorra aleatório, pontos de início aleatórios para cada equipe.\n\nMasmorra 1 ou 2: um andar dessa masmorra, criado pelo jogo com uma nova semente. O mesmo mapa para todos, sem monstros, sem itens e sem portal.',
   'Это поселение: карта мира PvP; команды начинают на двух её концах.\n\nСлучайная карта: случайный этаж подземелья, случайные точки старта для каждой команды.\n\nПодземелье 1 или 2: этаж этого подземелья, созданный игрой из нового зерна. Одна карта для всех, без монстров, предметов и портала.',
   '此据点：PvP 世界的地图，队伍从两端出发。\n\n随机地图：随机的地下城楼层，每队的出发点也随机。\n\n地下城 1 或 2：游戏用新种子生成的该地下城楼层。所有玩家同一张地图，没有怪物、物品和传送门。',
   '此定居點：PvP 世界的地圖，隊伍從兩端出發。\n\n隨機地圖：隨機的地下城樓層，每隊的出發點也隨機。\n\n地下城 1 或 2：遊戲用新種子產生的該地下城樓層。所有玩家同一張地圖，沒有怪物、物品和傳送門。'],
 'Type a name for the preset first.': ['Gib zuerst einen Namen für die Vorlage ein.', 'Primero escribe un nombre para el preajuste.', "Tapez d'abord un nom pour le préréglage.",
                                       '先にプリセット名を入力してください。', '먼저 프리셋 이름을 입력하세요.', 'Digite primeiro um nome para a predefinição.',
                                       'Сначала введите название набора.', '请先输入预设名称。', '請先輸入預設名稱。'],
 'Which floor of the dungeon: a random one, floor 1 to 4, or the boss room (a small arena with no boss in it).': [
   'Welche Ebene des Dungeons: eine zufällige, Ebene 1 bis 4 oder der Bossraum (eine kleine Arena ohne Boss).',
   'Qué piso de la mazmorra: uno aleatorio, del piso 1 al 4, o la sala del jefe (una arena pequeña sin jefe).',
   "Quel étage du donjon : un au hasard, l'étage 1 à 4, ou la salle du boss (une petite arène sans boss).",
   'ダンジョンのどの階か：ランダム、1～4階、またはボス部屋（ボスのいない小さなアリーナ）。',
   '던전의 어느 층인지: 무작위, 1~4층, 또는 보스 방(보스가 없는 작은 아레나).',
   'Qual andar da masmorra: um aleatório, do andar 1 ao 4, ou a sala do chefe (uma arena pequena sem chefe).',
   'Какой этаж подземелья: случайный, с 1 по 4 или комната босса (маленькая арена без босса).',
   '地下城的哪一层：随机、第 1 到 4 层，或首领房间（没有首领的小竞技场）。', '地下城的哪一層：隨機、第 1 到 4 層，或首領房間（沒有首領的小競技場）。'],
 'You have %d presets: delete one first.': ['Du hast %d Vorlagen: Lösche zuerst eine.', 'Tienes %d preajustes: borra uno primero.', 'Vous avez %d préréglages : supprimez-en un d\'abord.',
                                            'プリセットが%d個あります。先に1つ削除してください。', '프리셋이 %d개 있습니다. 먼저 하나를 삭제하세요.', 'Você tem %d predefinições: exclua uma primeiro.',
                                            'У вас %d наборов: сначала удалите один.', '你已有 %d 个预设：请先删除一个。', '你已有 %d 個預設：請先刪除一個。'],
 'Your saved sets of rules. The arrows pick one and apply its rules. Custom: the rules shown are not a saved preset.': [
   'Deine gespeicherten Regelsätze. Die Pfeile wählen einen und wenden seine Regeln an. Eigene: Die angezeigten Regeln sind keine gespeicherte Vorlage.',
   'Tus conjuntos de reglas guardados. Las flechas eligen uno y aplican sus reglas. Personalizado: las reglas mostradas no son un preajuste guardado.',
   'Vos jeux de règles enregistrés. Les flèches en choisissent un et appliquent ses règles. Personnalisé : les règles affichées ne sont pas un préréglage enregistré.',
   '保存したルールのセットです。矢印で選ぶとそのルールが適用されます。カスタム：表示中のルールは保存済みのプリセットではありません。',
   '저장한 규칙 세트입니다. 화살표로 하나를 고르면 그 규칙이 적용됩니다. 사용자 지정: 표시된 규칙은 저장된 프리셋이 아닙니다.',
   'Seus conjuntos de regras salvos. As setas escolhem um e aplicam as regras dele. Personalizado: as regras mostradas não são uma predefinição salva.',
   'Ваши сохранённые наборы правил. Стрелки выбирают набор и применяют его правила. Свои: показанные правила не являются сохранённым набором.',
   '你保存的规则组。用箭头选择一个即可应用其规则。自定义：显示的规则不是已保存的预设。', '你儲存的規則組。用箭頭選擇一個即可套用其規則。自訂：顯示的規則不是已儲存的預設。'],
}


def main():
    en = json.load(open(os.path.join(W, 'lang', 'en.json')))
    for k in T: assert k in en, 'not in en.json: ' + k
    for i, code in enumerate(L):
        p = os.path.join(W, 'lang', code + '.json'); d = json.load(open(p)); n = 0
        for k, v in T.items():
            if k not in d: d[k] = v[i]; n += 1
        json.dump(d, open(p, 'w'), ensure_ascii=False, indent=1); open(p, 'a').write('\n')   # file order kept, new keys at the end
        print(code, n, 'added')


if __name__ == '__main__':
    main()
