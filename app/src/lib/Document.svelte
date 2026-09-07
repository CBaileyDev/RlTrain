<script lang="ts">
  import { marked } from 'marked';
  import DOMPurify from 'dompurify';
  let { text, onLink }: { text: string; onLink: (href: string) => void } = $props();
  const html = $derived(
    DOMPurify.sanitize(marked.parse(text, { async: false }), {
      FORBID_TAGS: ['style', 'iframe', 'form', 'input', 'button'],
    }),
  );
  function links(node: HTMLElement) {
    const click = (event: MouseEvent) => {
      const link = (event.target as HTMLElement).closest('a');
      if (link) {
        event.preventDefault();
        onLink(link.getAttribute('href') ?? '');
      }
    };
    node.addEventListener('click', click);
    return { destroy: () => node.removeEventListener('click', click) };
  }
</script>

<article class="document" data-selectable use:links>{@html html}</article>
