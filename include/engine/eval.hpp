#pragma once

// Statik değerlendirme (evaluation): materyal + piece-square table, oyun fazına
// göre orta oyun (middlegame) / oyun sonu (endgame) interpolasyonu ile.
// Puan santipiyon (centipawn) cinsinden ve HAMLE SIRASI olan tarafın
// bakış açısından döner (pozitif = sıradaki taraf için iyi).
//
// PST değerleri Michniewski "Simplified Evaluation Function" (Chess
// Programming Wiki) tablolarından. Ham tablolar görsel düzende (8. sıra üstte,
// a-h soldan sağa) yazılmıştır; LERF kare indeksine derleme zamanında çevrilir.
//
// Tapered eval: her taş MG ve EG puanına ayrı katkı verir; nihai puan, kalan
// taşlardan hesaplanan "faz" (phase) ile bu iki uçtan interpolasyondur.
// Şu an MG ve EG tabloları yalnızca ŞAH'ta farklı (orta oyunda kenara/roka,
// oyun sonunda merkeze), materyal her iki fazda eşit. Sonraki adımlarda
// (pawn structure, king safety, mobility) diğer terimler bu çerçeveye eklenecek.

#include <array>
#include <string>
#include <vector>

#include "engine/types.hpp"

namespace engine {

struct Board;  // board.hpp'de struct olarak tanımlı — MSVC isim düzenlemesi
               // için class/struct anahtarı eşleşmeli.

// Taş başına materyal değeri (santipiyon). Şah 0 (her iki tarafta da var).
// Materyal şu an fazdan bağımsız (MG = EG); ileride ayrılabilir.
inline constexpr int MaterialValue[PIECE_TYPE_NB] = {
    100,  // PAWN
    320,  // KNIGHT
    330,  // BISHOP
    500,  // ROOK
    900,  // QUEEN
    0     // KING
};

// Oyun fazı ağırlıkları: kalan taşların toplam ağırlığı fazı verir. Başlangıç
// dizilimi tam kadro = 24 (orta oyun ucu); taşlar azaldıkça 0'a (oyun sonu ucu)
// iner. Piyon ve şah faza katkı vermez (her oyun sonuna kadar sahnede).
inline constexpr int PhaseWeight[PIECE_TYPE_NB] = {
    0,  // PAWN
    1,  // KNIGHT
    1,  // BISHOP
    2,  // ROOK
    4,  // QUEEN
    0   // KING
};

// Tam kadro faz değeri: 4*at? Hayır — renk başına değil, toplam: 4 at + 4 fil +
// 4 kale*2 + 2 vezir*4 = 4 + 4 + 8 + 8 = 24.
inline constexpr int MAX_PHASE = 24;

// --- Pawn structure ağırlıkları (santipiyon, MG/EG ayrı; tapered) ---
// Değerler standart/güvenli aralıkta; net Elo katkısı SPRT ile doğrulanır,
// tek tek ince ayar sonraki iş. Cezalar negatif, bonuslar pozitif.

// İzole piyon (komşu sütunlarda dost piyon yok) — piyon başına ceza.
inline constexpr int IsolatedPenaltyMg = -12;
inline constexpr int IsolatedPenaltyEg = -15;

// Çift piyon (aynı sütunda birden fazla) — ilk piyondan fazlası başına ceza.
// Oyun sonunda daha ağır (hareketsiz, geçemeyen piyon zayıflığı belirginleşir).
inline constexpr int DoubledPenaltyMg = -10;
inline constexpr int DoubledPenaltyEg = -20;

// Geçer piyon bonusu, piyonun SIRASINA (rank index 0-7) göre; ilerledikçe artar.
// Oyun sonunda belirgin daha büyük — tapered eval'in esas kazandığı yer.
// İndeks: beyaz için rank_of(sq), siyah için 7 - rank_of(sq). Uçlar (0,7) piyon
// için imkânsız, 0 bırakıldı.
inline constexpr int PassedBonusMg[8] = {0, 5, 10, 15, 25, 40, 60, 0};
inline constexpr int PassedBonusEg[8] = {0, 10, 20, 35, 60, 90, 120, 0};

// Geri piyon (backward) — piyon başına ceza (tapered, EG daha ağır). Bir piyon
// GERİ'dir eğer: (a) komşu sütun(lar)da dost piyon VAR (izole değil — ayrı terim)
// ama hepsi ONDAN İLERİDE (onu geriden destekleyecek dost piyon yok), VE (b) durak
// karesi (önündeki kare) bir rakip piyon tarafından kontrol ediliyor (güvenle
// ilerleyemez). Saf-piyon -> pawn_structure cache'ine girer (isole/çift/geçer gibi).
//
// ÜÇ SORU (bkz. EN KRİTİK DERS): (1) adıyla sayılmıyor — izole (komşu YOK) / çift
// (aynı sütun) / geçer farklı sinyaller; geri piyon "komşu VAR ama ileride + durak
// tıkalı" = yeni eksen. (2) sonucuyla: mevcut hiçbir terim "ilerleyemeyen desteksiz
// piyon"u fiyatlamaz (mobility piyonu saymaz; PST sıra-bazlı, tıkanmışlığı görmez)
// -> ortogonal. (3) işaret DAİMA negatif (geri piyon zayıflıktır), tek predicate ->
// işaret-tutarsızlık yok. Modest ilk elle-seçim (izole/çift emsali), E7 tuning adayı.
inline constexpr int BackwardPenaltyMg = -8;
inline constexpr int BackwardPenaltyEg = -12;

// Bağlı / falanks piyon (connected) — SIRA-ÖLÇEKLİ, YALNIZ İLERLEMİŞ (tapered, EG ağır).
// Bir piyon "connected"tir: (a) FALANKS — komşu sütunda aynı sıradaki dost piyon, VEYA
// (b) SUPPORTED — dost piyonca diagonal savunuluyor. Bonus = weight × factor, burada
// factor = max(0, göreli_sıra − 2): 2-3. sıra 0, 4→1, 5→2, 6→3, 7→4. Saf-piyon ->
// pawn_structure cache'ine girer.
//
// KRİTİK TASARIM — ORTOGONAL KALINTIYI hedefler (izole terimiyle örtüşmeyi keser):
// "connected" sinyalinin BÜYÜK kısmı izole terimiyle örtüşür (bağlı piyon inşa gereği
// izole DEĞİL -> izole cezası zaten uygulanmıyor; protected passer −22.1 emsali). İki
// koruma: (1) SIRA GATE rr>=3 -> düşük sıralarda (izole'nin baskın olduğu yerde) terim
// SUSAR; (2) "phalanx VEYA supported" = "sadece izole-değil"den DAR (uzak komşulu ama
// desteklenmeyen piyon connected DEĞİL). Kalıntı: izole'nin DÜZ cezasının hafife aldığı
// İLERLEMİŞ bağlı ikili/zincir (elle-hesap: d5+e5 phalanx vs d5+e7 loose, ikisi de izole
// değil -> mevcut eval ayırmıyor, connected ayırır).
//
// ÜÇ SORU: (1) adıyla sayılmıyor; (2) düşük-sıra örtüşmesi gate ile kesildi, kalıntı
// ortogonal — ANCAK ilerlemiş connected çoğu zaman PASSED de olur -> passed'in kısmi
// yeniden-ifadesi RİSKİ (enstrümantasyonda izlenir, ÖN-KAYITLI); (3) işaret + (ilerlemiş
// bağlı piyon iyi), tek predicate. Modest ilk elle-seçim, E7 tuning adayı.
inline constexpr int ConnectedBonusMg = 2;
inline constexpr int ConnectedBonusEg = 4;

// --- Mobility ağırlıkları (santipiyon, MG/EG ayrı; taş türü başına) ---
// Taşın ulaşabildiği (dost taşla dolu OLMAYAN) kare sayısına doğrusal bonus.
// Piyon ve şah hariç: piyon yapısı ayrı ele alınır, şah "mobilitesi" güvenlikle
// ters düşer. Vezir ağırlığı düşük — erken vezir çıkışını ödüllendirmemek için.
// Kale EG'de daha değerli (açık hatlar oyun sonunda kritik).
// İndeks taş türüdür (PAWN..KING).
inline constexpr int MobilityMg[PIECE_TYPE_NB] = {0, 4, 4, 2, 1, 0};
inline constexpr int MobilityEg[PIECE_TYPE_NB] = {0, 4, 4, 4, 2, 0};

// --- Bishop pair + rook on open/semi-open file ağırlıkları (MG/EG ayrı) ---
// Bishop pair: iki (veya daha fazla) fili olan tarafa bonus; açık oyun sonunda
// (uzun diyagonaller) biraz daha değerli.
inline constexpr int BishopPairMg = 30;
inline constexpr int BishopPairEg = 45;
// Kale açık sütunda (o sütunda hiç piyon yok) / yarı-açık sütunda (yalnız rakip
// piyon var). Açık sütun orta oyunda saldırı için daha değerli.
inline constexpr int RookOpenMg = 25;
inline constexpr int RookOpenEg = 15;
inline constexpr int RookSemiMg = 12;
inline constexpr int RookSemiEg = 8;

// --- Threats / hanging ağırlıkları (santipiyon, MG/EG ayrı; tapered) ---
// Rakip taşlara yönelen tehditler (piyon/minör/kale saldırısı + savunmasız taş).
// Bonuslar yalnız tehdit VARKEN uygulanır -> dengeli pozisyonda renkler arası ~iptal.
// Modest tutulur (eval-ölçek ↔ arama-marjı bağı: cp-kalibre RFP/futility/null marjları
// dondurulmuş; ölçeği kaydıran değişiklik onları bozar). Değerler ilk elle-seçim,
// E7 SPSA/Texel adayı; net Elo SPRT ile doğrulanır.
inline constexpr int ThreatByPawnMg  = 40;  // piyon vuruşu altındaki rakip N/B/R/Q
inline constexpr int ThreatByPawnEg  = 30;
inline constexpr int ThreatByMinorMg = 25;  // at/fil atağı altındaki rakip R/Q (minör → majör)
inline constexpr int ThreatByMinorEg = 20;
inline constexpr int ThreatByRookMg  = 20;  // kale atağı altındaki rakip Q (kale → vezir)
inline constexpr int ThreatByRookEg  = 15;
inline constexpr int HangingMg       = 25;  // vurulmuş + savunmasız rakip N/B/R/Q
inline constexpr int HangingEg       = 25;

// --- Outpost ağırlıkları (santipiyon, MG/EG ayrı; tapered) ---
// Outpost = dost piyonla DESTEKLENEN + rakip piyonun (kendi sütununda ilerleyerek)
// KOVAMAYACAĞI ileri kare. Eval'de bugüne dek ne piyon-desteği ne kovulabilirlik
// sayılıyordu (PST yalnız merkezîliği, mobility yalnız kare sayısını verir) ->
// ortogonal sinyal: ileri bir minörün KALICILIĞI.
//
// At > fil (2:1): at kısa menzilli, kalıcı-korunan kare uzağa baskının tek yolu;
// fil zaten diyagonalden çalışır -> fil için sinyalin çoğu PST/mobility'de.
//
// MG > EG: outpost bir ORTA OYUN kavramı — kalıcı ileri karedeki at, tahtada taş
// varken rakip pozisyona baskı yayar; oyun sonunda belirleyici olan şah aktifliği ve
// geçer piyonlardır. Bu bir satranç gerekçesi, ölçüm değil; ağırlıklar E7 (Texel/SPSA)
// adayı.
//
// ÇÜRÜTÜLEN BEKLENTİ (ölçüldü, geçici enstrümantasyon — commit'te yok): tasarımda
// "kovulamaz koşulu oyun sadeleştikçe bedavaya doğru olur -> terim EG'de şişer, o yüzden
// EG ağırlığı kısılmalı" deniyordu. YANLIŞ: at başına ateşleme oranı fazlar arası DÜZ
// (%2.49 orta oyun / %2.70 oyun sonu). Sebep: rakip piyon azalınca kovulabilirlik
// gerçekten gevşiyor (rank_ok %27 -> %52) ama AYNI anda dost piyon desteği zorlaşıyor
// (support_ok %47 -> %20) -> iki etki birbirini götürüyor. Destek koşulu terimi kendi
// kendine sınırlıyor; EG'de ölçek kayması riski yok.
inline constexpr int OutpostKnightMg = 25;
inline constexpr int OutpostKnightEg = 15;
inline constexpr int OutpostBishopMg = 12;
inline constexpr int OutpostBishopEg = 8;

// --- Geçer piyon ŞAH ESKORTU (YALNIZ oyun sonu; mg her zaman 0) ---
// Kendi şahımız geçer piyonun DURAK KARESİNE rakip şahtan DAHA YAKINSA bonus:
//   eg += PasserKingEscortEg * max(0, d_rakip − d_kendi)
// Mesafe = Chebyshev (şah hamlesi), 5'e kırpılır (öteye pratik fark yok + terimi
// sınırlar; Stockfish king_proximity de min(dist,5) yapar). Yalnız göreli sıra >= 3
// (4. sıra ve ötesi): 2. sıradaki piyonda şah mesafesi henüz anlamsız (SF: > RANK_3).
//
// TEK YÖNLÜ (max(0, ...)) — KRİTİK TASARIM KARARI, ÖLÇÜMLE ZORUNLU KILINDI:
// İlk tasarım simetrik fark formuydu (eg += w * (d_rakip − d_kendi)). SPRT ÖNCESİ
// enstrümantasyon onu ELEDİ: `d_rakip − d_kendi` SIRA ile GÜÇLÜ NEGATİF KORELE, çünkü
// durak karesi TANIMI GEREĞİ rakip sahasındadır -> rakip şah neredeyse daima daha yakın.
// Ölçüm: orta oyunda kendi şahımız 25169 vakanın YALNIZ 5'inde (%0.02) daha yakındı;
// beyaz sıra r'de: d_kendi ≈ r+1, d_rakip ≈ 6−(r+1) -> fark ≈ 4 − 2r (sıra 6'da −8).
// Yani simetrik form, "geçer piyona sahip olmayı" sistematik cezalandırıp elle-kalibre
// passed[r] bonusuyla KAVGA ediyordu (ölçüldü: sum/çağrı −5.0 cp oyun sonunda) —
// tempo (−18.6) / tune-all (−110) mekanizmasının aynısı: kalibre sisteme sistematik
// kayma enjekte etmek. max(0,...) tabanı SIFIRA sabitler: normal durumda ceza YOK,
// yalnız eskort BAŞARILDIĞINDA bonus -> passed[] ile kavga edemez.
//
// YALNIZ EG (king_safety'nin tam aynası — o MG-only): orta oyunda şah piyon eskortuna
// çıkmaz, güvenlik baskındır. Yan fayda: MG ölçek sorusu hiç doğmuyor.
//
// ÜÇ SORU (bkz. EN KRİTİK DERS): (1) adıyla sayılmıyor; (2) sonucuyla? EG şah PST'si
// merkezîliği ödüllendirir -> MERKEZİ passer'da "şaha yakın" ile korele, ama KANAT
// passer'ında TERSİNE döner (a5'e gitmek merkezden uzaklaşmak = PST cezası) -> kısmi,
// işareti değişken örtüşme; ayrıca sıra-korelasyonu max(0,...) ile kesildi;
// (3) işaret: terim yalnız TEK alt-kümede (kendi şah daha yakın) ateşler ve orada
// işaret tartışmasız pozitif -> işaret-tutarsızlık (blockade −12.4) riski yapısal olarak
// yok. Ağırlık E7 tuning adayı.
//
// AĞIRLIK ÖLÇÜLEREK SEÇİLDİ (SPRT öncesi kapı): ilk değer 4 idi -> oyun sonu abs/çağrı
// 0.658 cp, sağlıklı bandın (2-6) altında (max(0,...) negatif yarıyı kestiği için
// efektif büyüklük yarılanıyor). 8'e çıkarıldı -> ~1.3 cp. Tavan yine de 8*5 = 40 EG
// (passer başına); Stockfish'in aynı terimi sıra-ölçekli ve ileri passer'da 10-20 kat
// daha büyük -> bu değer hâlâ temkinli tarafta.
inline constexpr int PasserKingEscortEg = 8;

// NOT (2026-07-17): geçer piyon blokajı (blockade) DENENDİ ve RAFA KALDIRILDI —
// SPRT H0 TAM RED (−12.4 ± 9.8, LOS %0.7, LLR −2.95, 2544 oyun). Kök sebep
// yeniden-ifade DEĞİL (sinyal gerçekten fiyatlanmamıştı) — predicate İŞARET-TUTARSIZ:
// at/fil blokajı piyon sahibinin aleyhine ama kale/vezir blokajı muhtemelen LEHİNE
// (kale pasifleşip piyona bağlanır), ve ölçüm oyun sonunda blokajların %84'ünün
// kale/vezir olduğunu gösterdi. Ayrıntı + neden varyant denenmediği: CLAUDE.md Blok E3.
// Bu commit'ten kalan LATENT ALTYAPI: pawn cache'in taşıdığı geçer piyon kümesi
// (pawn_structure_full + PawnBucket.passed_w/passed_b) — şah mesafesi ve
// rook-behind-passer için hazır, EXACT ve maliyeti ölçülmüş ~0.

// --- KALE KENDİ GEÇER PİYONUNUN ARKASINDA (rook-behind-passer; YALNIZ oyun sonu) ---
// Tarrasch kuralı: kaleler geçer piyonun ARKASINA aittir. Kendi kalemiz kendi geçer
// piyonumuzun DURAK yönünün tersinde (arkasında), aynı sütunda, ARADA taş olmadan
// (temiz hat) duruyorsa piyonun ilerleyişini destekler:
//   eg += RookBehindPasserEg   (passer başına en fazla bir kez)
//
// YALNIZ EG (mg her zaman 0; escort/king_safety aynası): rook-behind-passer bir oyun
// sonu kavramı — geçer piyonun terfi yarışı oyun sonunda belirleyicidir; orta oyunda
// kale aktivitesi/güvenlik baskındır. Yan fayda: frozen MG arama-marjları (RFP/futility/
// null/razor) kaymaz -> eval-ölçek ↔ arama-marjı bağı sorunu doğmaz.
//
// YALNIZ ROOK (vezir değil): sinyali terim adıyla hizalı + temiz tut (vezir mobil,
// "arkada bekleme" değeri zayıf; Stockfish de güçlü bonusu kaleye verir).
//
// KAPSAM = yalnız KENDİ kale (kullanıcı kararı 2026-07-17). Rakip kalenin bizim
// passer'ımızın arkasında olması (Tarrasch'ın diğer yarısı) bilinçle DIŞARIDA: o alt-terim
// rakibin rook_on_file YARI-AÇIK bonusuyla kısmen örtüşürdü (soru-2 riski). Escort'un
// "tek-yönlü max(0,)" disiplininin aynısı: en temiz sinyalle başla.
//
// ÜÇ SORU (bkz. EN KRİTİK DERS): (1) adıyla sayılmıyor — rook_on_file yalnız o sütunda
// dost piyon YOKKEN ateşler; kendi passer'ımızın arkasındaki kale için dost piyon o
// sütunda VAR -> rook_on_file = 0, sıfır örtüşme. (2) sonucuyla sayılmıyor — aynı piyon
// yapısı + arkada kale var/yok iki pozisyonu mevcut eval ÖZDEŞ puanlar (kale PST ~düz,
// rook_on_file kapalı, passed_eg piyon-saf). (3) işaret — tek predicate (kendi kale),
// daima + (bonus); blockade'in işaret-tutarsızlığı yapısal olarak yok.
//
// Göreli sıra >= 3 kapısı (escort ile tutarlı): yalnız ilerlemiş passer'lar (2-3.
// sırada kale-arkada marjinal). AĞIRLIK ilk elle-seçim, enstrümantasyonla doğrulanır
// (SPRT öncesi kapı) + E7 tuning adayı.
inline constexpr int RookBehindPasserEg = 20;

// --- KÖTÜ FİL (bad bishop; tapered, EG daha ağır) ---
// Bir fil, kendi kare-renginde (aynı renk) duran dost piyonlar tarafından
// "kötüleştirilir": bu piyonlar filin diyagonallerini tıkar, fil onları asla
// savunamaz/geçemez. Fil başına ceza:
//   pen = BadBishop{Mg,Eg} * (#fil-renginde dost piyon)
//       + BadBishopBlocked{Mg,Eg} * (#bunlardan BLOKELİ olan; önündeki kare dolu)
// Ceza o rengin skorunu düşürür (king_safety danger deseni: += -sign*pen).
//
// TAPERED, EG DAHA AĞIR (kullanıcı kararı): kötü fil hem orta oyun hem oyun sonu
// kavramı, en çok oyun sonunda (piyonlar sabit, fil kalitesi belirleyici). tempo
// (−18.6, ÜNİFORM kayma) aksine bu pozisyona-bağlı ve kabaca simetrik -> ortalama
// static_eval'i neredeyse hiç kaydırmaz -> frozen MG marj (RFP/null/futility) kaplin
// riski düşük (outpost/bishop_pair/threats de tapered ve geçtiler).
//
// BLOKELİ piyon AĞIR TARTILIR (kullanıcı kararı — ortogonallik): mobility filin ANLIK
// tıkalı diyagonallerini zaten sayar (soru 2 riski); bloke piyon ise mobility'nin
// (tek-ply snapshot) hafife aldığı KALICI yapısal zaafiyet -> en keskin ortogonal sinyal.
//
// ÜÇ SORU (bkz. EN KRİTİK DERS): (1) adıyla sayılmıyor — hiçbir terim piyon-fil-rengi
// ilişkisini saymaz; (2) sonucuyla KISMİ örtüşme (mobility anlık blok) -> bloke-ağırlık
// bunu azaltır (elle-kontrol: aynı yapıda iyi-renk vs kötü-renk fil pozisyonlarını mevcut
// eval yalnız ~2 cp ayırıyor, işareti bile ters [PST]; sinyal yeni); (3) işaret DAİMA
// negatif (fil renginde çok piyon her zaman kötü), tek predicate -> blockade-tarzı
// işaret-tutarsızlığı yapısal olarak yok.
//
// AĞIRLIK ÖLÇÜLEREK KISILDI (SPRT öncesi kapı, geçici enstrümantasyon — commit'te YOK):
// ilk değer 2/3/3/5 idi -> NET |katkı|/çağrı orta oyunda mg 7.0 / eg 11.0 cp (escort 1.3,
// outpost 2.0'ın 3-5 katı) ve fil'lerin %99.99'unda ateşliyordu (ortalama 3.77 aynı-renk
// piyon = NORMAL fil de cezalanıyor). NET işaretli/çağrı ≈ 0 (renk-simetrik -> ölçek
// KAYMIYOR; tempo/tune-all failure mode'u yapısal yok). Yine de büyüklük yüksek + ayrımcı
// değil -> modest banda ~yarıya çekildi: 1/2/2/3 (NET |katkı|/çağrı ~mg 3.9 / eg 2.7).
// Bloke hâlâ ağır (blocked_mg 2>base 1, blocked_eg 3>base 2), EG hâlâ ağır. E7 joint
// tuning weights'i geri yukarı çıkarabilir (kalibre ile).
inline constexpr int BadBishopMg        = 1;
inline constexpr int BadBishopEg        = 2;
inline constexpr int BadBishopBlockedMg = 2;
inline constexpr int BadBishopBlockedEg = 3;

// --- King safety ağırlıkları (YALNIZ orta oyun; EG=0 -> taper ile solar) ---
// Oyun sonunda şah merkeze/aktifliğe yönelir (KingCentralizedInEndgame), güvenlik
// önemsizleşir; bu yüzden king_safety yalnız MG'ye katkı verir (eg her zaman 0).
//
// Piyon kalkanı: şahın önündeki (renk yönünde iki sıra) kf-1..kf+1 sütunlarında
// dost piyon yoksa, o sütun için "danger" (tehlike, pozitif santipiyon) eklenir.
inline constexpr int ShieldMissingPenalty = 15;  // eksik kalkan sütunu başına
// Şah kenarda değilse (kf-1 veya kf+1 tahta içindeyse) her sütun sayılır; tahta
// kenarındaki geçersiz sütun eksik sayılmaz (aşağıdaki döngü atlar).

// Şah bölgesi (king ring) saldırıları -> "attack units". Rakip taş türü başına
// ağırlık, o taşın şah halkasında vurduğu kare sayısıyla çarpılır (CPW "King
// Safety" örneği). Küçük ağırlıklar kasıtlı: kare-sayısıyla çarpılıp toplanır.
inline constexpr int KingAttackWeight[PIECE_TYPE_NB] = {
    0,  // PAWN  (piyon baskısı ayrı ele alınmıyor)
    2,  // KNIGHT
    2,  // BISHOP
    3,  // ROOK
    5,  // QUEEN
    0   // KING
};

// SafetyTable[units] -> orta oyun "danger" cezası (santipiyon), doğrusal değil:
// düşük baskıda ~0 (normal pozisyonlar cezalanmaz), baskı arttıkça hızla büyür,
// ~çeyrek vezir eşiğinde 500'de doygunlaşır. CPW "King Safety" standart tablosu.
// İndeks 0..99 (units bu aralığa kırpılır).
inline constexpr int SafetyTable[100] = {
      0,   0,   1,   2,   3,   5,   7,   9,  12,  15,
     18,  22,  26,  30,  35,  39,  44,  50,  56,  62,
     68,  75,  82,  85,  89,  97, 105, 113, 122, 131,
    140, 150, 169, 180, 191, 202, 213, 225, 237, 248,
    260, 272, 283, 295, 307, 319, 330, 342, 354, 366,
    377, 389, 401, 412, 424, 436, 448, 459, 471, 483,
    494, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
    500, 500, 500, 500, 500, 500, 500, 500, 500, 500
};

namespace detail {

// Ham PST tabloları (orta oyun), görsel düzen: indeks 0 = a8, indeks 63 = h1.
inline constexpr int RawPST[PIECE_TYPE_NB][SQUARE_NB] = {
    // PAWN
    {  0,  0,  0,  0,  0,  0,  0,  0,
      50, 50, 50, 50, 50, 50, 50, 50,
      10, 10, 20, 30, 30, 20, 10, 10,
       5,  5, 10, 25, 25, 10,  5,  5,
       0,  0,  0, 20, 20,  0,  0,  0,
       5, -5,-10,  0,  0,-10, -5,  5,
       5, 10, 10,-20,-20, 10, 10,  5,
       0,  0,  0,  0,  0,  0,  0,  0 },
    // KNIGHT
    { -50,-40,-30,-30,-30,-30,-40,-50,
      -40,-20,  0,  0,  0,  0,-20,-40,
      -30,  0, 10, 15, 15, 10,  0,-30,
      -30,  5, 15, 20, 20, 15,  5,-30,
      -30,  0, 15, 20, 20, 15,  0,-30,
      -30,  5, 10, 15, 15, 10,  5,-30,
      -40,-20,  0,  5,  5,  0,-20,-40,
      -50,-40,-30,-30,-30,-30,-40,-50 },
    // BISHOP
    { -20,-10,-10,-10,-10,-10,-10,-20,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5, 10, 10,  5,  0,-10,
      -10,  5,  5, 10, 10,  5,  5,-10,
      -10,  0, 10, 10, 10, 10,  0,-10,
      -10, 10, 10, 10, 10, 10, 10,-10,
      -10,  5,  0,  0,  0,  0,  5,-10,
      -20,-10,-10,-10,-10,-10,-10,-20 },
    // ROOK
    {  0,  0,  0,  0,  0,  0,  0,  0,
       5, 10, 10, 10, 10, 10, 10,  5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
      -5,  0,  0,  0,  0,  0,  0, -5,
       0,  0,  0,  5,  5,  0,  0,  0 },
    // QUEEN
    { -20,-10,-10, -5, -5,-10,-10,-20,
      -10,  0,  0,  0,  0,  0,  0,-10,
      -10,  0,  5,  5,  5,  5,  0,-10,
       -5,  0,  5,  5,  5,  5,  0, -5,
        0,  0,  5,  5,  5,  5,  0, -5,
      -10,  5,  5,  5,  5,  5,  0,-10,
      -10,  0,  5,  0,  0,  0,  0,-10,
      -20,-10,-10, -5, -5,-10,-10,-20 },
    // KING (orta oyun): şah kenarda/rok bölgesinde güvenli, merkez tehlikeli.
    { -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -30,-40,-40,-50,-50,-40,-40,-30,
      -20,-30,-30,-40,-40,-30,-30,-20,
      -10,-20,-20,-20,-20,-20,-20,-10,
       20, 20,  0,  0,  0,  0, 20, 20,
       20, 30, 10,  0,  0, 10, 30, 20 }
};

// Şah oyun-sonu tablosu (görsel düzen): oyun sonunda şah merkeze/aktif olmalı.
// Michniewski "king end game" tablosu. Diğer taşların EG tablosu MG ile aynı.
inline constexpr int RawKingEg[SQUARE_NB] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

// Ham görsel-düzen tabloyu LERF-indeksli (beyaz) tabloya çevirir.
constexpr std::array<int, SQUARE_NB> to_lerf(const int (&raw)[SQUARE_NB]) {
    std::array<int, SQUARE_NB> t{};
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        int rank = sq >> 3, file = sq & 7;
        int vis  = (7 - rank) * 8 + file;  // LERF -> görsel
        t[sq] = raw[vis];
    }
    return t;
}

// Beyaz için LERF-indeksli PST (orta oyun). Siyah için dikey ayna: [sq ^ 56].
constexpr std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> make_pst_mg() {
    std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> t{};
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        t[pt] = to_lerf(RawPST[pt]);
    return t;
}

// Oyun-sonu PST: ŞAH dışında MG ile aynı; ŞAH oyun-sonu tablosunu kullanır.
constexpr std::array<std::array<int, SQUARE_NB>, PIECE_TYPE_NB> make_pst_eg() {
    auto t = make_pst_mg();
    t[KING] = to_lerf(RawKingEg);
    return t;
}

// --- Pawn structure bitboard maskeleri (derleme zamanı) ---

// Her sütunun (file 0-7) sekiz karesini içeren bitboard.
constexpr std::array<Bitboard, 8> make_file_masks() {
    std::array<Bitboard, 8> t{};
    for (int f = 0; f < 8; ++f) {
        Bitboard bb = 0;
        for (int r = 0; r < 8; ++r)
            bb |= Bitboard{1} << (r * 8 + f);
        t[f] = bb;
    }
    return t;
}

// Bir sütunun komşu sütunları (f-1 ve f+1, tahta içinde). İzole tespiti için:
// piyonun sütununun komşularında dost piyon yoksa piyon izoledir.
constexpr std::array<Bitboard, 8> make_adjacent_file_masks() {
    auto files = make_file_masks();
    std::array<Bitboard, 8> t{};
    for (int f = 0; f < 8; ++f) {
        Bitboard bb = 0;
        if (f > 0) bb |= files[f - 1];
        if (f < 7) bb |= files[f + 1];
        t[f] = bb;
    }
    return t;
}

// Geçer piyon "önündeki alan" maskesi: sq'nun sütunu + komşu sütunlar, yalnızca
// ilerideki sıralar (beyaz için rank > rank_of(sq), siyah için rank <). Bu maske
// altında RAKİP piyon yoksa piyon geçerdir (durduramaz/yakalayamazlar).
constexpr std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> make_passed_masks() {
    std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> t{};
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        int f = sq & 7, r = sq >> 3;
        Bitboard w = 0, b = 0;
        for (int nf = f - 1; nf <= f + 1; ++nf) {
            if (nf < 0 || nf > 7) continue;
            for (int nr = 0; nr < 8; ++nr) {
                Bitboard bit = Bitboard{1} << (nr * 8 + nf);
                if (nr > r) w |= bit;  // beyaz ilerisi (yukarı)
                if (nr < r) b |= bit;  // siyah ilerisi (aşağı)
            }
        }
        t[WHITE][sq] = w;
        t[BLACK][sq] = b;
    }
    return t;
}

// Kare rengi maskeleri: koyu kareler (file+rank çift; a1 = koyu = 0), açık kareler
// tümleyeni. Bad bishop terimi: filin durduğu karenin rengindeki dost piyonları saymak
// için. [0] = koyu, [1] = açık -> index (file_of(sq)+rank_of(sq))&1 ile seçilir.
constexpr std::array<Bitboard, 2> make_square_color_masks() {
    std::array<Bitboard, 2> t{};
    for (int sq = 0; sq < SQUARE_NB; ++sq) {
        int f = sq & 7, r = sq >> 3;
        t[(f + r) & 1] |= Bitboard{1} << sq;
    }
    return t;
}

}  // namespace detail

// Beyaz bakışıyla orta oyun / oyun sonu PST. Siyah için dikey ayna: [pt][sq ^ 56].
inline constexpr auto PstMg = detail::make_pst_mg();
inline constexpr auto PstEg = detail::make_pst_eg();

// Pawn structure maskeleri (derleme zamanı, beyaz+siyah).
inline constexpr auto FileMask         = detail::make_file_masks();
inline constexpr auto AdjacentFileMask = detail::make_adjacent_file_masks();
inline constexpr auto PassedMask       = detail::make_passed_masks();

// Kare rengi maskeleri (bad bishop): [0] = koyu kareler, [1] = açık kareler.
inline constexpr auto SquareColorMask  = detail::make_square_color_masks();

// --- Tunable eval parametreleri (Blok 4: Texel tuning) ---
// Yukarıdaki elle-seçilmiş constexpr sabitler artık yalnız VARSAYILAN kaynağı;
// evaluate() ve terim fonksiyonları çalışma-zamanı MUTABLE `g_eval`'den okur, böylece
// tuner ağırlıkları arama/eval'i yeniden derlemeden değiştirebilir. Varsayılan hâlde
// g_eval bu sabitlerle birebir aynıdır -> davranış-koruyan (exact) refactor.
//
// KAPSAM: king safety alanları (shield/attack/safety_table) struct'a alındı ama ilk
// Texel geçişinde DONDURULUR (doğrusal olmayan SafetyTable ayrı ele alınacak).
// game_phase ağırlıkları (PhaseWeight/MAX_PHASE) yapısal -> tunable değil, constexpr.
// SEE + delta pruning eval'den bağımsız kendi `MaterialValue`'sunu (dondurulmuş)
// kullanmaya devam eder; buradaki `material` yalnız evaluate() içindir.
struct EvalParams {
    int material[PIECE_TYPE_NB];                 // taş materyal değeri (evaluate için)
    int pst_mg[PIECE_TYPE_NB][SQUARE_NB];        // orta oyun PST (LERF, beyaz bakışı)
    int pst_eg[PIECE_TYPE_NB][SQUARE_NB];        // oyun sonu PST (MG'den bağımsız)

    int isolated_mg, isolated_eg;                // izole piyon cezası
    int doubled_mg,  doubled_eg;                 // çift piyon cezası
    int passed_mg[8], passed_eg[8];              // geçer piyon bonusu (rank index)

    int mobility_mg[PIECE_TYPE_NB], mobility_eg[PIECE_TYPE_NB];  // mobility ağırlığı

    int bishop_pair_mg, bishop_pair_eg;          // fil çifti bonusu
    int rook_open_mg,  rook_open_eg;             // kale açık sütun
    int rook_semi_mg,  rook_semi_eg;             // kale yarı-açık sütun

    // Threats / hanging (tunable; frozen sınırın önünde -> tune edilir).
    int threat_by_pawn_mg,  threat_by_pawn_eg;   // piyon vuruşu altındaki rakip taş
    int threat_by_minor_mg, threat_by_minor_eg;  // minör atağı altındaki rakip majör
    int threat_by_rook_mg,  threat_by_rook_eg;   // kale atağı altındaki rakip vezir
    int hanging_mg,         hanging_eg;          // vurulmuş + savunmasız rakip taş

    // Outpost (tunable; frozen sınırın önünde). İsimli skaler — dizi ([PIECE_TYPE_NB])
    // olsaydı 12 param giderdi, 8'i yapısal ölü ağırlık (piyon/kale/vezir/şah outpost
    // almaz); threats bloğu da isimli skaler.
    int outpost_knight_mg, outpost_knight_eg;    // desteklenen + kovulamayan at
    int outpost_bishop_mg, outpost_bishop_eg;    // desteklenen + kovulamayan fil

    // Geçer piyon şah eskortu (tunable). YALNIZ EG (mg yok). PIYON-SAF DEĞİL
    // (şah yerine bağlı) -> pawn cache'e girmez; geçer piyon KÜMESİ cache'ten gelir.
    int passer_king_escort_eg;                   // eg += w * max(0, d_rakip − d_kendi)

    // Kale kendi geçer piyonunun arkasında (tunable). YALNIZ EG. PIYON-SAF DEĞİL
    // (kaleye bağlı) -> pawn cache'e girmez; geçer piyon KÜMESİ cache'ten gelir.
    int rook_behind_passer_eg;                   // eg += w (passer başına, temiz hat)

    // Kötü fil (tunable; frozen sınırın önünde; tapered). PIYON-DIŞI occupancy'ye
    // (bloke testi) bağlı -> pawn cache'e GİRMEZ (attack_eval_impl'de hesaplanır).
    int bad_bishop_mg,         bad_bishop_eg;     // fil-renginde dost piyon başına ceza
    int bad_bishop_blocked_mg, bad_bishop_blocked_eg;  // bunlardan BLOKELİ olana EK ceza

    // Geri piyon (tunable; frozen sınırın önünde). SAF-PIYON -> pawn_structure cache'ine
    // girer (izole/çift/geçer gibi).
    int backward_mg, backward_eg;                // ilerleyemeyen + desteksiz piyon cezası

    // Bağlı/falanks piyon (tunable; frozen sınırın önünde). SAF-PIYON -> cache. Sıra-ölçekli
    // (bonus = weight × max(0, göreli_sıra − 2)), yalnız ilerlemiş; phalanx VEYA supported.
    int connected_mg, connected_eg;              // ilerlemiş bağlı piyon bonusu

    // King safety (yalnız MG; ilk geçişte dondurulur).
    int shield_missing;                          // eksik kalkan sütunu başına ceza
    int king_attack_weight[PIECE_TYPE_NB];       // şah bölgesi saldırı ağırlığı
    int safety_table[100];                       // attack units -> danger (doğrusal değil)
};

// Elle-seçilmiş varsayılan ağırlıklarla dolu EvalParams üretir (yukarıdaki
// constexpr sabitlerden + PstMg/PstEg tablolarından kopyalar).
EvalParams make_default_eval_params();

// Global, çalışma-zamanı eval parametreleri. Varsayılanla başlar; tuner (chess tune)
// ya da parametre dosyası bunu değiştirebilir. evaluate() + terim fonksiyonları okur.
extern EvalParams g_eval;

// --- Düz (flat) parametre haritası: kaydet/yükle VE tuner için tek doğruluk kaynağı ---
// Her tunable skaler için bir işaretçi (p içine) + eşleşen isim, SABİT sırada döner.
// Sıra: material, pst_mg, pst_eg, pawn, mobility, bishop_pair, rook_file, sonra
// (dondurulmuş) king safety: shield, attack_weight, safety_table. King safety
// bloğu KASITLA en sonda -> tuner yalnız [0, eval_frozen_start()) aralığını tune eder.
std::vector<int*>               flat_param_pointers(EvalParams& p);
const std::vector<std::string>& flat_param_names();
// King safety (dondurulmuş) parametrelerinin başladığı düz indeks. Tuner bu indeksin
// altındaki parametreleri tune eder, üstündekilere (king safety) dokunmaz.
int                             eval_frozen_start();

// EvalParams'ı düz metin dosyaya kaydeder / dosyadan yükler. Format: satır başına
// "<flat-isim> <değer>". Yükleme isimle eşler (sıra bağımsız, eksik alan varsayılan
// kalır). Başarılıysa true.
bool save_eval_params(const EvalParams& p, const std::string& path);
bool load_eval_params(EvalParams& p, const std::string& path);

// Pozisyonun oyun fazı [0, MAX_PHASE]: MAX_PHASE = tam kadro (orta oyun ucu),
// 0 = yalnız şah+piyon (oyun sonu ucu).
int game_phase(const Board& b);

// Pawn structure katkısı (izole + çift + geçer piyon), BEYAZ − SİYAH bakışıyla,
// MG ve EG ayrı out-param olarak. evaluate() bunu akümülatörlerine ekler; ayrıca
// terimleri PST gürültüsü olmadan izole test etmek için doğrudan çağrılabilir.
void pawn_structure(const Board& b, int& mg, int& eg);

// pawn_structure'ın tam sürümü: MG/EG'ye EK OLARAK her rengin geçer piyon KÜMESİNİ
// de döndürür. Pawn hash cache tam olarak bunu saklar (küme piyon-saftır); passer
// rafineleri (blokaj vb.) kümeyi cache'ten alır, yeniden üretmez (ölçüldü: yeniden
// üretim %3-5 nps). pawn_structure(b,mg,eg) bunun ince sarmalayıcısıdır.
void pawn_structure_full(const Board& b, int& mg, int& eg,
                         Bitboard& passed_w, Bitboard& passed_b);

// Geri piyon katkısı (ilerleyemeyen + geriden desteklenemeyen piyon), BEYAZ − SİYAH,
// MG/EG ayrı (tapered). Negatif = o taraf için zayıflık. Saf-piyon -> pawn_structure_full
// içinden çağrılır (cache'e girer); izole test için doğrudan da çağrılabilir.
void backward_pawns(const Board& b, int& mg, int& eg);

// Bağlı/falanks piyon katkısı (ilerlemiş phalanx VEYA desteklenen piyon, sıra-ölçekli),
// BEYAZ − SİYAH, MG/EG ayrı (tapered). Pozitif = o taraf için avantaj. Saf-piyon ->
// pawn_structure_full içinden çağrılır (cache'e girer); izole test için doğrudan da çağrılabilir.
void connected_pawns(const Board& b, int& mg, int& eg);

// Geçer piyon şah eskortu katkısı, BEYAZ − SİYAH. mg HER ZAMAN 0 (yalnız oyun sonu
// terimi; taper ile orta oyunda solar — king_safety'nin aynası). PIYON-SAF DEĞİL ->
// pawn cache'e girmez; geçer piyon kümesi cache'ten gelir (_with sürümü).
// Bu sarmalayıcı kümeyi kendisi üretir -> YALNIZ izole test için.
void passer_king_escort(const Board& b, int& mg, int& eg);

// Asıl sürüm: geçer piyon kümeleri dışarıdan (pawn cache'ten) verilir.
void passer_king_escort_with(const Board& b, Bitboard passed_w, Bitboard passed_b,
                             int& mg, int& eg);

// Kale kendi geçer piyonunun arkasında katkısı, BEYAZ − SİYAH. mg HER ZAMAN 0 (yalnız
// oyun sonu). PIYON-SAF DEĞİL (kaleye bağlı) -> pawn cache'e girmez; geçer piyon kümesi
// cache'ten gelir (_with sürümü). Bu sarmalayıcı kümeyi kendisi üretir -> YALNIZ izole test.
void rook_behind_passer(const Board& b, int& mg, int& eg);

// Asıl sürüm: geçer piyon kümeleri dışarıdan (pawn cache'ten) verilir.
void rook_behind_passer_with(const Board& b, Bitboard passed_w, Bitboard passed_b,
                             int& mg, int& eg);

// Mobility katkısı (at/fil/kale/vezir ulaşılabilir kare sayısı), BEYAZ − SİYAH,
// MG/EG ayrı out-param. evaluate() akümülatörlerine ekler; izole test edilebilir.
void mobility(const Board& b, int& mg, int& eg);

// Bishop pair katkısı (iki fili olan tarafa bonus), BEYAZ − SİYAH, MG/EG ayrı.
void bishop_pair(const Board& b, int& mg, int& eg);

// Kale açık/yarı-açık sütun katkısı, BEYAZ − SİYAH, MG/EG ayrı.
void rook_on_file(const Board& b, int& mg, int& eg);

// Threats / hanging katkısı (rakip taşlara piyon/minör/kale tehditleri + savunmasız
// taş), BEYAZ − SİYAH, MG/EG ayrı (tapered). evaluate() akümülatörlerine ekler;
// izole test edilebilir. Atak setleri mobility/king_safety ile TEK GEÇİŞTE paylaşılır.
void threats(const Board& b, int& mg, int& eg);

// Outpost katkısı (dost piyonla desteklenen + rakip piyonun kendi sütununda
// ilerleyerek kovamayacağı ileri karedeki at/fil), BEYAZ − SİYAH, MG/EG ayrı
// (tapered). evaluate() akümülatörlerine ekler; izole test edilebilir. Hesap
// mobility/king_safety/threats ile TEK GEÇİŞTE (attack_eval_impl) yapılır.
void outpost(const Board& b, int& mg, int& eg);

// Kötü fil katkısı (fil-renginde dost piyonlar + bunlardan blokeli olanlara ek ceza),
// BEYAZ − SİYAH, MG/EG ayrı (tapered). Negatif = filin sahibi için ceza. Hesap
// mobility/king_safety/threats/outpost ile TEK GEÇİŞTE (attack_eval_impl); izole test.
void bad_bishop(const Board& b, int& mg, int& eg);


// King safety katkısı (piyon kalkanı + şah bölgesi saldırıları), BEYAZ − SİYAH.
// eg her zaman 0 (yalnız orta oyun terimi); mg negatif = beyaz şahı daha güvensiz.
void king_safety(const Board& b, int& mg, int& eg);

// evaluate()'in ara toplamları: BEYAZ bakışıyla orta oyun (mg) ve oyun sonu (eg)
// akümülatörleri, taper ve side-to-move flip'inden ÖNCE. Tuner bunu kullanır
// (taper katsayısı game_phase'den gelir; parametreler bu toplamlarda doğrusaldır).
void eval_accumulate(const Board& b, int& mg_white, int& eg_white);

// Hamle sırası olan tarafın bakışından statik değerlendirme (santipiyon).
int evaluate(const Board& b);

// Pawn hash cache anahtarı (varsayılan true). Tuner finite-difference sırasında
// false yapmalı: perturbe edilmiş pawn ağırlıkları için cache bayat değer döndürür
// -> pawn gradyanları bozulur.
extern bool g_pawn_cache_enabled;

}  // namespace engine
