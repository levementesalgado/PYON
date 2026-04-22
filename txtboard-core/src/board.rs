use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Post {
    pub _id: String,
    pub board: String,
    pub id: u64,
    pub author_pubkey: String,
    pub author_name: Option<String>,
    pub subject: Option<String>,
    pub body: String,
    pub images: Vec<ImageRef>,
    pub reply_to: Option<u64>,
    pub sig: String,
    pub created_at: DateTime<Utc>,
    pub edited_at: Option<DateTime<Utc>>,
}

impl Post {
    /// Bytes canônicos para assinatura — igual ao Ruby: "board|id|body|subject"
    pub fn canonical(board: &str, id: u64, body: &str, subject: &str) -> Vec<u8> {
        format!("{}|{}|{}|{}", board, id, body, subject).into_bytes()
    }
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct ImageRef {
    pub filename: String,
    pub size_bytes: u64,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub mime: String,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct BoardMeta {
    pub slug: String,
    pub title: String,
    pub description: String,
    pub category: BoardCategory,
    pub post_count: u64,
    pub last_post_at: Option<DateTime<Utc>>,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub enum BoardCategory {
    Padrao, Anime, Universitario, Cultura, Direto,
}

pub fn all_boards() -> Vec<BoardMeta> {
    use BoardCategory::*;
    let now = None;
    vec![
        bm("sr",    "sala da república",   "board padrão",              Padrao,       0, now),
        bm("meta",  "sobre o txtboard",    "regras · sugestões",        Padrao,       0, now),
        bm("a",     "anime",               "anime geral",               Anime,        0, now),
        bm("m",     "mangá",               "mangá · light novel",       Anime,        0, now),
        bm("fan",   "fanart · doujinshi",  "fanart · pixiv",            Anime,        0, now),
        bm("vn",    "visual novels",       "vns · eroge",               Anime,        0, now),
        bm("fig",   "figuras",             "figures · nendos · merch",  Anime,        0, now),
        bm("ost",   "trilhas · j-music",   "ost · jpop · vocaloid",     Anime,        0, now),
        bm("rep",   "república",           "moradia · kitnet",          Universitario,0, now),
        bm("aula",  "estudos",             "provas · resumos",          Universitario,0, now),
        bm("ic",    "inic. científica",    "pesquisa · bolsas",         Universitario,0, now),
        bm("rango", "comida",              "bandejão · delivery",       Universitario,0, now),
        bm("estag", "estágio",             "emprego · carreira",        Universitario,0, now),
        bm("café",  "desabafo",            "textão · vent",             Universitario,0, now),
        bm("hist",  "história",            "história geral",            Cultura,      0, now),
        bm("arte",  "artes visuais",       "pintura · ilustração",      Cultura,      0, now),
        bm("lit",   "literatura",          "livros · poesia",           Cultura,      0, now),
        bm("fil",   "filosofia",           "ética · epistemologia",     Cultura,      0, now),
        bm("music", "música",              "teoria · bandas",           Cultura,      0, now),
        bm("cine",  "cinema · séries",     "filmes · análises",         Cultura,      0, now),
        bm("arq",   "arquitetura",         "design · urbanism",         Cultura,      0, now),
        bm("foto",  "fotografia",          "analógica · digital",       Cultura,      0, now),
        bm("tech",  "tecnologia",          "código · hardware",         Direto,       0, now),
        bm("sci",   "ciência",             "exatas e naturais",         Direto,       0, now),
        bm("jp",    "japão",               "cultura jp · idioma",       Direto,       0, now),
        bm("br",    "brasil",              "cotidiano · cultura",       Direto,       0, now),
        bm("jogo",  "jogos",               "games · tabletop · rpg",    Direto,       0, now),
        bm("livr",  "livros · quadrinhos", "hq · graphic novel",        Direto,       0, now),
        bm("id",    "identidade",          "flags · comunidade",        Direto,       0, now),
        bm("diy",   "faça você mesmo",     "craft · eletrônica",        Direto,       0, now),
        bm("comfy", "comfy",               "slice of life · vibe",      Direto,       0, now),
    ]
}

fn bm(slug:&str,title:&str,desc:&str,cat:BoardCategory,
      count:u64,last:Option<DateTime<Utc>>) -> BoardMeta {
    BoardMeta { slug:slug.to_string(), title:title.to_string(),
                description:desc.to_string(), category:cat,
                post_count:count, last_post_at:last }
}
