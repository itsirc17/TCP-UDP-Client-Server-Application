# PROTOCOALE DE COMUNICATIE Tema 2 - Aplicatie client-server TCP si UDP pentru gestionarea mesajelor

## DESCRIERE GENERALĂ
Aceasta aplicatie implementeaza un server care face legatura intre:
- Clienti UDP (publicatori) care trimit mesaje pe diverse topic-uri
- Clienti TCP (subscriberi) care se aboneaza la topic-uri si primesc mesaje

### IMPLEMENTARE

#### INSPIRATIE LAB7
Punctul de plecare al acestei teme a fost laboratorul 7 (Protocolul TCP. Multiplexare IO.)
Functiile de send_all, recv_all, modul cum este folosita multiplexarea si functiile pe sockets, dar si logica din main sunt implementate in exact acelasi mod in care am rezolvat si laboratorul.

#### FUNCTIONALITATE SPECIFICA TEMEI
Datorita necesitatii ca aplicatia sa fie rapida si eficienta, folosesc urmatoarele map-uri:
- unordered_map <int, string> socketToIdMap;
- unordered_map <string, Connected> connected;
- unordered_multimap <string, string> topicToIdMap;

Astfel, in momentul in care se formeaza o conexiune noua TCP, tinem minte portul pe care se afla ID ul sau. Daca clientul se deconecteaza, continuam sa tinem minte ID ul acestuia, tinand cont in map ul connected ca este deconectat, iar daca acesta se reconecteaza actualizam socket ul pe care se afla. Astfel se pot forma mai multe conexiuni cu mai multi clienti TCP atata timp cat nu se repeta ID ul.

In momentul in care un client TCP conectat trimite un mesaj subscribe/unsibscribe este actualizat multimap ul topicToIdMap. Astfel tinem cont care topic trebuie transmis caror ID uri. 

In momentul in care vine un mesaj de pe socket ul UDP, in functie de tip formam mesajul pentru a-l trimite mai departe la clientii abonati la topic ul mesajului.

##### PROTOCOL APLICATIE PESTE TCP
Principalul mod in care se trimit mesaje de la server la client este urmatorul:
Pentru a preveni concatenarea mesajelor, serverul trimite intai lungimea mesajului (4 octeti, in network byte order), iar apoi trimite si mesajul propriu-zis, de lungime exacta.

In acelasi mod, daca serverul se inchide, sau daca un client incearca sa se conecteze, dar exista deja un client cu ID ul respectiv conectat, serverul trimite un mesaj catre server pentru a-si opri conexiunea.


Wildcards (* și +) nu sunt implementate