"""
rag.py

Implements Retrieval-Augmented Generation (RAG) using ChromaDB as the vector store.
This module processes PDF manuals by chunking their text, converting them into 
vector embeddings, and storing them in a local ChromaDB instance.
During a user query, it retrieves the top chunks most relevant to the user's transcript 
to provide context to the LLM.

Installation Requirements:
  pip install chromadb sentence-transformers pypdf

Expected Directory Structure:
  server/knowledge_base/   <- Place PDF documentation files here
  server/chroma_db/        <- Automatically generated directory for the vector store
"""

import os
import logging
from pathlib import Path
from typing import Optional

log = logging.getLogger("rag")

# Paths and configuration constants for the RAG pipeline
KB_DIR    = Path(__file__).parent.parent / "rag_pdf"
CHROMA_DIR = Path(__file__).parent / "chroma_db"
COLLECTION = "lab_docs"

# Chunking and retrieval tuning parameters
CHUNK_SIZE      = 400   # The target length for text chunks (in characters)
CHUNK_OVER      = 50    # The number of overlapping characters between consecutive chunks
TOP_K           = 3     # The maximum number of relevant chunks to retrieve for a query
MAX_CHUNKS_PDF  = 200   # Cap the number of chunks per PDF to prevent large documents from dominating

# Global instances for the database client, collection, and the sentence embedder
_client     = None
_collection = None
_embedder   = None


def _get_embedder():
    """
    Lazily loads and returns the SentenceTransformer model used for creating text embeddings.
    """
    global _embedder
    if _embedder is None:
        from sentence_transformers import SentenceTransformer
        log.info("Loading embedding model (paraphrase-multilingual-MiniLM-L12-v2)...")
        # Load a multilingual embedding model capable of handling multiple languages effectively
        _embedder = SentenceTransformer("paraphrase-multilingual-MiniLM-L12-v2")
        log.info("Embedder is ready.")
    return _embedder


def _get_collection():
    """
    Initializes and returns the ChromaDB collection used for storing and querying documents.
    """
    global _client, _collection
    if _collection is None:
        import chromadb
        
        # Ensure the directory for the database exists
        CHROMA_DIR.mkdir(exist_ok=True)
        
        # Initialize a persistent client storing data locally
        _client = chromadb.PersistentClient(path=str(CHROMA_DIR))
        
        # Access the collection, configuring it to use cosine similarity for distance calculations
        _collection = _client.get_or_create_collection(
            name=COLLECTION,
            metadata={"hnsw:space": "cosine"},
        )
        log.info(f"ChromaDB is ready. Currently storing {_collection.count()} chunks.")
    return _collection


def _chunk_text(text: str) -> list[str]:
    """
    Splits a large block of text into smaller chunks of CHUNK_SIZE characters,
    ensuring a CHUNK_OVER overlap between adjacent chunks to maintain context continuity.
    """
    chunks = []
    start = 0
    while start < len(text):
        end = min(start + CHUNK_SIZE, len(text))
        chunks.append(text[start:end].strip())
        # Advance the start pointer by the chunk size minus the overlap amount
        start += CHUNK_SIZE - CHUNK_OVER
        
    # Filter out extremely small or empty chunks that might add noise
    return [c for c in chunks if len(c) > 50]


def index_pdfs() -> int:
    """
    Scans the designated knowledge base directory for PDF files, extracts their text,
    splits it into manageable chunks, and indexes them into ChromaDB.
    
    It checks if a PDF has already been indexed (by looking for its first chunk ID) 
    to avoid redundant processing.
    
    Returns:
        The total number of text chunks added to the database.
    """
    try:
        from pypdf import PdfReader
    except ImportError:
        log.error("The pypdf library is not installed. Please install it using: pip install pypdf")
        return 0

    KB_DIR.mkdir(exist_ok=True)
    col = _get_collection()
    emb = _get_embedder()

    added = 0
    # Process every PDF file found in the directory
    for pdf_path in KB_DIR.glob("*.pdf"):
        try:
            # Check if this PDF is already indexed by querying for the ID of its first chunk
            first_id = f"{pdf_path.stem}_0"
            existing = col.get(ids=[first_id], include=[])
            if existing["ids"]:
                log.info(f"PDF document is already indexed, skipping: {pdf_path.name}")
                continue

            # Read the PDF and concatenate text from all of its pages
            reader = PdfReader(str(pdf_path))
            full_text = "\n".join(
                page.extract_text() or "" for page in reader.pages
            )
            
            # Break the full document text down into smaller chunks
            chunks = _chunk_text(full_text)
            
            # Enforce a maximum chunk limit per PDF document
            if len(chunks) > MAX_CHUNKS_PDF:
                chunks = chunks[:MAX_CHUNKS_PDF]
            log.info(f"Processing PDF: {pdf_path.name} -> Generated {len(chunks)} chunks.")

            # Generate unique identifiers and metadata for each chunk
            ids   = [f"{pdf_path.stem}_{i}" for i in range(len(chunks))]
            vecs  = emb.encode(chunks, show_progress_bar=False).tolist()
            metas = [{"source": pdf_path.name, "chunk": i} for i in range(len(chunks))]

            # Insert or update the chunks in the vector database
            col.upsert(ids=ids, embeddings=vecs, documents=chunks, metadatas=metas)
            added += len(chunks)

        except Exception as e:
            log.error(f"Failed to read or process PDF ({pdf_path.name}): {e}")

    log.info(f"RAG Indexing complete: {added} new chunks added or updated. "
             f"Total chunks in database: {col.count()}")
    return added


def query(text: str, n_results: int = TOP_K) -> Optional[str]:
    """
    Searches the vector database for PDF paragraphs that semantically match the user's transcript.
    
    Arguments:
    - text: The user's input query.
    - n_results: The maximum number of chunks to return.
    
    Returns:
    - A formatted string combining the retrieved text chunks along with their source,
      or None if the database is empty or the results are not sufficiently relevant.
    """
    try:
        col = _get_collection()
        if col.count() == 0:
            return None

        emb = _get_embedder()
        
        # Convert the user query into a vector representation
        vec = emb.encode([text], show_progress_bar=False).tolist()
        
        # Query the database for the closest matching chunks
        results = col.query(
            query_embeddings=vec,
            n_results=min(n_results, col.count()),
            include=["documents", "metadatas", "distances"],
        )

        # Extract the resulting data lists
        docs  = results["documents"][0]
        metas = results["metadatas"][0]
        dists = results["distances"][0]

        if not docs:
            return None

        # Filter the results by applying a stricter distance threshold.
        # A smaller distance value means higher similarity (cosine distance).
        # Anything above 0.45 is deemed irrelevant and excluded from the context.
        relevant = [
            (d, m["source"], dist)
            for d, m, dist in zip(docs, metas, dists)
            if dist < 0.45
        ]

        if not relevant:
            return None

        # Format the retrieved relevant chunks to present to the LLM
        parts = []
        for doc, src, _ in relevant:
            parts.append(f"[{src}]: {doc}")

        context = "\n---\n".join(parts)
        log.info(f"RAG Query: Found {len(relevant)} relevant chunks.")
        
        return context

    except Exception as e:
        log.error(f"Error executing RAG query: {e}", exc_info=True)
        return None
