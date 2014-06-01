SIK_3
=====
Zadanie zaliczeniowe --- E-ognisko

(Zmiany w treści zadania są zaznaczone na ten zielonawy kolor)

Fajnie byłoby móc zorganizować ognisko... wirtualne, takie z gitarą lub karaoke. Napisz w dwa programy: serwer i klient, dzięki którym będzie można przekazywać dźwięk (lub inne dane) pomiędzy wieloma klientami.

"Zmienne" użyte w treści

    PORT - numer portu, z którego korzysta serwer do komunikacji (zarówno TCP, jak i UDP), domyślnie 10000 + (numer_albumu % 10000); ustawiany parametrem -p serwera, opcjonalnie też klient (patrz opis)
    SERVER_NAME - nazwa lub adres IP serwera, z którym powinien połączyć się klient, ustawiany parametrem -s klienta
    FIFO_SIZE - rozmiar w bajtach kolejki FIFO, którą serwer utrzymuje dla każdego z klientów; ustawiany parametrem -F serwera, domyślnie 10560
    FIFO_LOW_WATERMARK - opis w treści; ustawiany parametrem -L serwera, domyślnie 0
    FIFO_HIGH_WATERMARK - opis w treści; ustawiany parametrem -H serwera, domyślnie równy FIFO_SIZE
    BUF_LEN - rozmiar (w datagramach) bufora pakietów wychodzących, ustawiany parametrem -X serwera, domyślnie 10
    RETRANSMIT_LIMIT - opis w treści; ustawiany parametrem -X klienta, domyślnie 10
    TX_INTERVAL - czas (w milisekundach) pomiędzy kolejnymi wywołaniami miksera, ustawiany parametrem -i serwera; domyślnie: 5ms

Serwer

Serwer to nic innego jak duży mikser --- odbiera dane od wielu klientów, następnie miesza je wszystkie, otrzymując w efekcie jeden strumień danych. Następnie przesyła zmieszane dane z powrotem do wszystkich klientów. Opisana procedura odbywa się przy pomocy protokołu UDP.

Serwer nasłuchuje na porcie PORT, zarówno na TCP, jak i na UDP. Połączenie TCP w zasadzie służy jedynie do wykrywania, że połączenie zostało przerwane. Protokół opisany jest dalej.

Dla każdego klienta serwer powinien utrzymywać kolejkę FIFO wielkości FIFO_SIZE bajtów. Kolejka może być w jednym z dwóch stanów:

    FILLING - stan początkowy, a także ustalany wtedy, gdy liczba bajtów w FIFO stanie się mniejsza bądź równa FIFO_LOW_WATERMARK,
    ACTIVE - stan ustalany wtedy, gdy liczba bajtów w FIFO stanie się większa bądź rowna FIFO_HIGH_WATERMARK.

Dane odebrane od klienta umieszczane są w kolejce z nim związanej. Jeśli w kolejce nie ma miejsca, serwer nie powinien próbować czytać danych od tego klienta.

Poza tym co TX_INTERVAL serwer konstruuje datagram wyjściowy, przekazując dane z tych kolejek, które są w stanie ACTIVE, do poniższej funkcji miksującej, która powinna być umieszczona w osobnym pliku.

struct mixer_input {
  void* data;       // Wskaźnik na dane w FIFO
  size_t len;       // Liczba dostępnych bajtów
  size_t consumed;  // Wartość ustawiana przez mikser, wskazująca, ile bajtów należy
                    // usunąć z FIFO.
} 

void mixer(
  struct mixer_input* inputs, size_t n,  // tablica struktur mixer_input, po jednej strukturze na każdą
                                         // kolejkę FIFO w stanie ACTIVE
  void* output_buf,                      // bufor, w którym mikser powinien umieścić dane do wysłania
  size_t* output_size,                   // początkowo rozmiar output_buf, następnie mikser umieszcza
                                         // w tej zmiennej liczbę bajtów zapisanych w output_buf
  unsigned long tx_interval_ms           // wartość zmiennej TX_INTERVAL
);

Domyślna implementacja powinna zakładać, że danymi w FIFO są liczby 16-bitowe ze znakiem, a na wyjście chcemy sumować odpowiadające sobie wartości. Każde wywołanie powinno wyprodukować dane długości 176*tx_interval_ms bajtów. Jeśli w którejś z kolejek jest mniej danych, trzeba założyć, że reszta to zera. Ewentualne przepełnienia należy obciąć do granicznych wartości typu 16-bitowego.

Zwrócone w output_buf/output_size dane serwer wysyła przez UDP do wszystkich klientów (datagramy są wysyłane nawet jeśli output_size == 0).

Należy zwrócić uwagę na to, żeby dane rzeczywiście były przesyłane średnio co TX_INTERVAL czasu, to ważne w przypadku danych audio. Często powtarzające się trzaski bądź przerwy mogą mieć związek z nieodpowiednią szybkością transmisji. Pojedyncze trzaski są dopuszczalne, trudno bowiem zapewnić powtarzalny czas odpowiedzi systemu na zdarzenia w skali, o której mowa w zadaniu. Będzie też pewna rozbieżność w szybkości zegarów pomiędzy komputerami w sieci. Prawdziwe systemy przesyłania multimediów korzystają z różnych algorytmów synchronizacji takich strumieni.

Serwer powinien się zakończyć po odebraniu sygnału SIGINT (Ctrl-C).
Klient

Klient przekazuje dane ze standardowego wejścia do serwera oraz jednocześnie z serwera na standardowe wyjście.

Jeśli klient otrzyma z linii komend argument -s, łączy się z serwerem o podanej nazwie (DNS-owej) lub adresie IP, na port przekazany ew. parametrem -p.

Po wyczerpaniu danych ze standardowego wejścia klient się nie kończy, natomiast nadal przyjmuje dane z serwera. Klient powinien się zakończyć po odebraniu sygnału SIGINT (Ctrl-C).

Numery portów klienta powinny być dynamicznie przydzielone przez system.
Protokół

Zdefiniowany poniżej format danych powinien umożliwiać komunikację między programami różnych autorów. Nie wolno zmieniać tego formatu ani go rozszerzać. W razie wątpliwości proszę pytać na forum.
Format strumienia TCP

Na początku klient nawiązuje połączenie TCP z serwerem. Po odebraniu połączenia serwer wysyła linijkę

CLIENT clientid\n

gdzie clientid jest 32-bitową liczbą (zapisaną tekstem), identyfikującą połączenie. Po odebraniu takiej linii klient inicjuje komunikację po UDP (patrz niżej). Następnie serwer okresowo, co ok. sekundę przesyła tym połączeniem raport o wszystkich aktywnych klientach, w następującym formacie:

[gniazdo TCP klienta] FIFO: [bieżąca liczba bajtów w FIFO]/[FIFO_SIZE] (min. [minimalna liczba bajtów w FIFO od ostatniego raportu], max. [maksymalna])\n

Raport powinien być poprzedzony sekwencją czyszczącą ekran (na terminalach uniksowych): "\x1b[H\x1b[2J" pustą linią.

Przykład:

\n
127.0.0.1:50546 FIFO: 6040/7040 (min. 824, max. 7040)\n
127.0.0.1:50547 FIFO: 442/7040 (min. 424, max. 4242)\n

Format datagramów UDP

CLIENT clientid\n

Wysyłany przez klienta jako pierwszy datagram UDP. Parametr clientid powinien być taki sam jak odebrany od serwera w połączeniu TCP.

UPLOAD nr\n
[dane]

Wysyłany przez klienta datagram z danymi. Nr to numer datagramu, zwiększany o jeden przy każdym kolejnym fragmencie danych. Taki datagram może być wysłany tylko po uprzednim odebraniu wartości ack (patrz ACK lub DATA) równej nr oraz nie może zawierać więcej danych niż ostatnio odebrana wartość win.

DATA nr ack win\n
[dane]

Wysyłany przez serwer datagram z danymi. Nr to numer datagramu, zwiększany o jeden przy każdym kolejnym fragmencie danych; służy do identyfikacji datagramów na potrzeby retransmisji. Ack to numer kolejnego datagramu (patrz UPLOAD) oczekiwanego od klienta, a win to liczba wolnych bajtów w FIFO.

Dane należy przesyłać w formie binarnej, dokładnie tak, jak wyprodukował je mikser.

ACK ack win\n
[dane]

Wysyłany przez serwer datagram potwierdzający otrzymanie danych. Jest wysyłany po odebraniu każdego poprawnego datagramu UPLOAD. Znaczenie ack i win -- patrz DATA.

RETRANSMIT nr\n

Wysyłana przez klienta do serwera prośba o ponowne przesłanie wszystkich dostępnych datagramów o numerach większych lub równych nr.

KEEPALIVE\n

Wysyłany przez klienta do serwera 10 razy na sekundę.
Wykrywanie kłopotów z połączeniem

W przypadku wykrycia kłopotów z połączeniem przez klienta, powinien on zwolnić wszystkie zasoby oraz rozpocząć swoje działanie od początku, automatycznie, nie częściej jednak niż 2x na sekundę. Tylko w przypadku kłopotów, co do których można oczekiwać, że bez sensu jest spróbować ponownie (np. zły format argumentów), klient powinien się zakończyć. Uznajemy, że w przypadku niemożności połączenia z serwerem, próbujemy do skutku.

W przypadku wykrycia kłopotów z połączeniem przez serwer, powinien on zwolnić wszystkie zasoby związane z tym klientem. Klient pewnie spróbuje ponownie.

Jeśli serwer przez sekundę nie odbierze żadnego datagramu UDP, uznaje połączenie za kłopotliwe. Zezwala się jednakże na połączenia TCP bez przesyłania danych UDP, jeśli się chce zobaczyć tylko raporty.

Jeśli klient przez sekundę nie otrzyma żadnych danych od serwera (choćby pustego datagramu DATA), uznaje połączenie za kłopotliwe.

Jeśli połączenie TCP zostanie przerwane, to też uznaje się je za kłopotliwe. :)
Retransmisje klient -> serwer

Jeśli klient otrzyma dwukotnie datagram DATA, bez potwierdzenia ostatnio wysłanego datagramu, powinien ponowić wysłanie ostatniego datagramu.
Retransmisje serwer -> klient

Serwer nadaje strumienie danych przy pomocy UDP. Może się zdarzyć zatem, że niektóre datagramy nie dotrą do adresata. Serwer powinien posiadać bufor (algorytmicznie to jest kolejka FIFO, jednakże unikam tej nazwy, żeby nie mylić z odbiorczymi kolejkami FIFO serwera), w którym pamięta ostatnich BUF_LEN nadanych datagramów.

Jeśli klient otrzyma datagram z numerem (ozn. nr_recv) większym niż kolejny oczekiwany (ozn. nr_expected), powinien:

    ów datagram porzucić, ale wysłać w odpowiedzi datagram "RETRANSMIT nr_expected", jeśli nr_expected >= nr_recv - RETRANSMIT_LIMIT,
    ów datagram przyjąć oraz uznać, że poprzedzających nie uda mu się nigdy odebrać (czyli nr_expected := nr_recv + 1) w p. p.

Jeśli klient otrzyma datagram z numerem mniejszym niż kolejny oczekiwany, powinien ów datagram porzucić.

Żeby zapobiec jendak nadmiarowi retransmisji, klient pamięta masymalny numer nr_max_seen otrzymanego datagramu DATA (licząc porzucone) i nigdy nie wysyła prośby o retransmisję w odpowiedzi na datagram, który nie zwiększa wartości nr_max_seen.

Uruchomienie klienta
Na Moodle'u znajduje się plik sik-zad-skrypty.tgz, który zawiera przykładowe skrypty pozwalające przekazywać dane dźwiękowe z/do klienta.
Polecenie sox konwertuje plik MP3 na strumień surowych danych (44100 4-bajtowych sampli na każdą sekundę pliku wejściowego).

Polecenie arecord i aplay można znaleźć w pakiecie alsa-utils.

Możliwe, że polecenia w skryptach wymagają dostosowania do używanej dystrybucji. W przypadku pracy na wolniejszym komputerze lub w maszynie wirtualnej konieczne może być zwiększenie rozmiarów buforów nagrywania i odwarzania (parametr -B), kosztem większych opóźnień. W razie kłopotów proszę pisać na forum.

Wymagana wydajność

Przy uruchomieniu 10-ciu klientów (przykładowymi skryptami) i serwera na lokalnym komputerze, a także w lokalnej sieci, średnie opóźnienie odtwarzania nie powinno być większe niż 100ms.

Praktycznie, nie teoretycznie, tzn. opóźnienie można zmierzyć jako różnicę czasu pomiędzy odczytaniem a wypisaniem tych samych danych przez klienta. Ten pomiar nie uwzględnia oczywiście opóźnień w linuksowym systemie dźwięku, i o to chodzi.
Bonus (+0,5 do oceny, z możliwością uzyskania 5!)

Do zaliczenia bonusu wymaga się, żeby końcowego efektu dało się wygodnie używać.

Bonus: napisać dodatkowo dwa skrypty (w dowolnym języku), które umożliwiają jednoczesne oglądanie filmu w kilku miejscach. Można założyć, że w owych miejscach znajdują się kopie tego samego filmu. Skrypt player powinien uruchomić odtwarzacz oraz:

    synchronizować pozycję filmu, zatrzymanie itp.
    przekazywać odgłosy, okrzyki, śmiechy oglądających, tak jak to robiłoby "normalne" e-ognisko

Co znaczy, że pewnie najłatwiej jest uruchomić dwie instancje systemu, po jednej na każdy z powyższych punktów. Dopuszcza się takie rozwiązanie.

Jako odtwarzacza można użyć np. MPlayera, którym można łatwo sterować ze skryptów (http://www.mplayerhq.hu/DOCS/tech/slave.txt). Proszę jednak pozostawić możliwość sterowania nim lokalnie, np. regulacji głośności, pełnego ekranu itp.

Skrypt serwer powinien uruchamiać serwery.

Do skryptów powinien być dołączony plik README z instrukcją obsługi.

Można założyć, że uczestnicy mają w miarę porządne domowe łącza internetowe, choć możliwe, że mimo tego do uzyskania zadowalającego efektu potrzeba użycia innych parametrów danych audio niż w przykładach. Należy przeprowadzić testy. System powinien rozsądnie działać dla pięciu osób. Możliwe, że trzeba coś poradzić na sumujący się szum od wszystkich uczestników.
Ustalenia wspólne i końcowe

Programy zaliczeniowe muszą być tak napisane, żeby obsługa poszczególnych gniazd nie przerywała obsługi pozostałych, tzn. obsługę połączeń należy zlecić osobnym procesom/wątkom lub zastosować mechanizm select/poll lub podobny. Należy także wyłączyć algorytm Nagle'a dla połączeń TCP. Można założyć, że standardowe wejście/wyjście procesów obsługuje select/poll (jeśli nie, można wypisać stosowny komunikat i zakończyć proces z błędem).

Programy powinny obsługiwać zarówno komunikację IPv4, jak i IPv6, jak jest to pokazane w materiałach do labów. Dotyczy to w szczególności obsługi parametru -s klienta.

Programy można pisać w C lub w C++(11). Należy używać API poznanych na laboratoriach, nie wykorzystywać bibliotek, które je opakowują, poza libevent oraz Boost:ASIO.

Ewentualne komunikaty diagnostyczne należy wypisywać na standardowe wyjście błędów.

Do rozwiązania należy dołączyć Makefile.

Pytania do treści proszę zadawać na forum. Mogą się tam też pojawiać istotne informacje, proszę więc czytać je na bieżąco.

Poza tym programy powinny działać rozsądnie, a nie tylko być zgodne z niniejszą treścią.
Termin

Termin oddania zadania to 20.05 25.05 23:59. Każde rozpoczęte 48h spóźnienia będzie kosztowało jeden punkt, jednak w sumie za spóźnienia nie będzie odjętych więcej niż 2p.
