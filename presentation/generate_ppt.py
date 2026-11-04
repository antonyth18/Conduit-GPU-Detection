import collections 
import collections.abc
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.enum.text import PP_ALIGN

def add_slide(prs, title, bullets):
    slide_layout = prs.slide_layouts[1] # Title and Content
    slide = prs.slides.add_slide(slide_layout)
    
    title_shape = slide.shapes.title
    title_shape.text = title
    
    body_shape = slide.shapes.placeholders[1]
    tf = body_shape.text_frame
    
    for i, bullet in enumerate(bullets):
        if i == 0:
            p = tf.paragraphs[0]
            p.text = bullet
        else:
            p = tf.add_paragraph()
            p.text = bullet
        
        # Adjust font size for neatness
        p.font.size = Pt(20)
        p.space_after = Pt(10)

def main():
    prs = Presentation()

    # Slide 1: Title Slide
    title_slide_layout = prs.slide_layouts[0]
    slide = prs.slides.add_slide(title_slide_layout)
    title = slide.shapes.title
    subtitle = slide.placeholders[1]

    title.text = "GPU-Accelerated Conflict Detection and\nDependency Graph Construction for\nParallel Smart Contract Execution using CUDA"
    title.text_frame.paragraphs[0].font.size = Pt(36)
    
    subtitle.text = (
        "Team Members:\n"
        "Aditya Raj (241IT006) | Antony Thaikadavil (241IT010)\n"
        "Nikhil Soni (241IT048) | Sucheth K Katte (241IT078)\n\n"
        "Department of Information Technology\n"
        "National Institute of Technology Karnataka, Surathkal"
    )
    for p in subtitle.text_frame.paragraphs:
        p.font.size = Pt(18)

    # Slide 2: Problem Statement & Motivation
    add_slide(prs, "Problem Statement & Motivation", [
        "Sequential Execution Bottleneck: Modern blockchains execute transactions sequentially, capping throughput despite transaction independence.",
        "Preprocessing Cost: Conflict detection and dependency graph construction become severe computational bottlenecks for large blocks (10k-100k txs).",
        "Limitations of Existing Systems: Current CPU-centric scheduling (e.g., Block-STM) scales poorly with increasing block sizes.",
        "Proposed Solution: Leverage massively parallel GPU computation (CUDA) to accelerate the preprocessing stage for parallel smart contract execution."
    ])

    # Slide 3: Proposed Pipeline & Architecture
    add_slide(prs, "Proposed Pipeline & Architecture", [
        "Phase 1 & 2: Workload Generation & Read/Write Extraction - thousands of CUDA threads extract state access patterns simultaneously.",
        "Phase 3: Conflict Detection - Identifying Write-Read and Write-Write intersections using GPU parallel processing to avoid O(n²) CPU bottlenecks.",
        "Phase 4 & 5: Dependency Graph Construction - Building optimized sparse graphs representing execution constraints using CSR and Hash Indexing.",
        "Phase 6: Parallel Scheduling - Identifying independent execution batches via connected components or topological sorting on GPUs."
    ])

    # Slide 4: Research Challenges & CUDA Implementation
    add_slide(prs, "Research Challenges & CUDA Implementation", [
        "Algorithmic Challenges: Avoiding naive all-pairs comparisons, optimizing irregular memory accesses, and balancing work across CUDA thread blocks.",
        "Handling Workload Skew: Efficiently managing popular accounts that create disproportionate conflicts.",
        "CUDA Components: Custom kernels for transaction analysis, conflict indexing, graph edge generation, and parallel prefix scan.",
        "Data Structures: Compressed Sparse Row (CSR) graphs, Hash tables, Bitmaps, and parallel vectors."
    ])

    # Slide 5: Expected Deliverables & Contributions
    add_slide(prs, "Expected Deliverables & Contributions", [
        "Core Deliverables: A complete CUDA-accelerated preprocessing pipeline (Conflict Detector, Dependency Constructor, Parallel Scheduler).",
        "Evaluation & Benchmarking: Comprehensive empirical study comparing CPU (Sequential/OpenMP) against GPU (CUDA) implementations.",
        "Performance Metrics: Preprocessing time, relative speedup, memory usage, and GPU utilization across varying conflict densities.",
        "Broader Impact: Quantifiable guidance for future parallel blockchain execution systems considering GPU offloading."
    ])

    prs.save('Project_Presentation.pptx')
    print("Presentation saved as Project_Presentation.pptx")

if __name__ == '__main__':
    main()
