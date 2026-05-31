"""
rag.py — ChromaDB tabanlı RAG (Retrieval-Augmented Generation)
PDF kılavuzları chunk'lar → embeder → ChromaDB'ye yazar.
Sorgu anında transcript'e en yakın 3 chunk'ı döndürür.

Kurulum:
  pip install chromadb sentence-transformers pypdf

Dizin yapısı:
  server/knowledge_base/   ← PDF dosyalarını buraya koy
  server/chroma_db/        ← otomatik oluşur (vektör deposu)
"""

import os
import logging
from pathlib import Path
from typing import Optional

log = logging.getLogger("rag")

KB_DIR    = Path(__file__).parent.parent / "rag_pdf"
CHROMA_DIR = Path(__file__).parent / "chroma_db"
COLLECTION = "lab_docs"
CHUNK_SIZE  = 400   # karakter
CHUNK_OVER  = 50    # örtüşme
TOP_K       = 3     # kaç chunk dönsün

_client     = None
_collection = None
_embedder   = None


def _get_embedder():
    global _embedder
    if _embedder is None:
        from sentence_transformers import SentenceTransformer
        log.info("Embedding modeli yükleniyor (paraphrase-multilingual-MiniLM-L12-v2)...")
        _embedder = SentenceTransformer("paraphrase-multilingual-MiniLM-L12-v2")
        log.info("Embedder hazır.")
    return _embedder


def _get_collection():
    global _client, _collection
    if _collection is None:
        import chromadb
        CHROMA_DIR.mkdir(exist_ok=True)
        _client = chromadb.PersistentClient(path=str(CHROMA_DIR))
        _collection = _client.get_or_create_collection(
            name=COLLECTION,
            metadata={"hnsw:space": "cosine"},
        )
        log.info(f"ChromaDB hazır: {_collection.count()} chunk yüklü")
    return _collection


def _chunk_text(text: str) -> list[str]:
    """Metni CHUNK_SIZE karakterlik parçalara böler, CHUNK_OVER örtüşmeyle."""
    chunks = []
    start = 0
    while start < len(text):
        end = min(start + CHUNK_SIZE, len(text))
        chunks.append(text[start:end].strip())
        start += CHUNK_SIZE - CHUNK_OVER
    return [c for c in chunks if len(c) > 50]


def index_pdfs() -> int:
    """
    knowledge_base/ klasöründeki tüm PDF'leri okur ve ChromaDB'ye ekler.
    Zaten indekslenmiş dosyalar atlanır (doc_id ile kontrol).
    Döndürür: eklenen chunk sayısı.
    """
    try:
        from pypdf import PdfReader
    except ImportError:
        log.error("pypdf yüklü değil: pip install pypdf")
        return 0

    KB_DIR.mkdir(exist_ok=True)
    col = _get_collection()
    emb = _get_embedder()

    added = 0
    for pdf_path in KB_DIR.glob("*.pdf"):
        try:
            reader = PdfReader(str(pdf_path))
            full_text = "\n".join(
                page.extract_text() or "" for page in reader.pages
            )
            chunks = _chunk_text(full_text)
            log.info(f"PDF: {pdf_path.name} → {len(chunks)} chunk")

            ids  = [f"{pdf_path.stem}_{i}" for i in range(len(chunks))]
            vecs = emb.encode(chunks, show_progress_bar=False).tolist()
            metas = [{"source": pdf_path.name, "chunk": i}
                     for i in range(len(chunks))]

            # Batch upsert — var olanlar güncellenir
            col.upsert(ids=ids, embeddings=vecs,
                       documents=chunks, metadatas=metas)
            added += len(chunks)

        except Exception as e:
            log.error(f"PDF okuma hatası ({pdf_path.name}): {e}")

    log.info(f"RAG index: {added} chunk eklendi/güncellendi. "
             f"Toplam: {col.count()}")
    return added


def query(text: str, n_results: int = TOP_K) -> Optional[str]:
    """
    Transcript'e en yakın PDF paragraflarını döndürür.
    ChromaDB boşsa veya hata varsa None döner.
    """
    try:
        col = _get_collection()
        if col.count() == 0:
            return None

        emb = _get_embedder()
        vec = emb.encode([text], show_progress_bar=False).tolist()
        results = col.query(
            query_embeddings=vec,
            n_results=min(n_results, col.count()),
            include=["documents", "metadatas", "distances"],
        )

        docs  = results["documents"][0]
        metas = results["metadatas"][0]
        dists = results["distances"][0]

        if not docs:
            return None

        # Mesafe > 0.8 ise ilgisiz — atla
        relevant = [
            (d, m["source"], dist)
            for d, m, dist in zip(docs, metas, dists)
            if dist < 0.8
        ]

        if not relevant:
            return None

        parts = []
        for doc, src, _ in relevant:
            parts.append(f"[{src}]: {doc}")

        context = "\n---\n".join(parts)
        log.info(f"RAG: {len(relevant)} chunk bulundu")
        return context

    except Exception as e:
        log.error(f"RAG sorgu hatası: {e}", exc_info=True)
        return None
