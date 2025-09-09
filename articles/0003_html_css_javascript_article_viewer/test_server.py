import pytest
from pathlib import Path
from fastapi.testclient import TestClient

# Import the server module under an alias to avoid name conflicts
import server as viewer_server


@pytest.fixture
def mock_fs(tmp_path, monkeypatch):
    """
    A pytest fixture to create a temporary, isolated filesystem for testing.
    It mocks the directory structure and patches the constants in the server
    module to use these temporary paths. This ensures tests are hermetic.
    """
    # 1. Create mock directories based on the real structure
    mock_home = tmp_path / "home" / "testuser"
    mock_articles_dir = mock_home / "articles"
    mock_viewer_dir = mock_articles_dir / "0003_html_css_javascript_article_viewer"
    mock_templates_dir = mock_viewer_dir / "templates"

    mock_articles_dir.mkdir(parents=True)
    mock_viewer_dir.mkdir()
    mock_templates_dir.mkdir()

    # 2. Monkeypatch the server's global path constants to use our mock paths
    monkeypatch.setattr(viewer_server, "HOME_DIR", mock_home)
    monkeypatch.setattr(viewer_server, "ARTICLES_DIR", mock_articles_dir)
    monkeypatch.setattr(viewer_server, "VIEWER_DIR", mock_viewer_dir)
    monkeypatch.setattr(viewer_server, "TEMPLATES_DIR", mock_templates_dir)

    # 3. Create a dummy index.html needed by the serve_frontend endpoint
    (mock_templates_dir / "index.html").write_text("<html>Hello Test</html>")

    return mock_articles_dir


# --- Unit Tests for Pure Functions ---

def test_format_title():
    """Tests the format_title utility function for various cases."""
    assert viewer_server.format_title("0001_basic_fastapi") == "Basic Fastapi"
    assert viewer_server.format_title("no_number_slug") == "No Number Slug"
    assert viewer_server.format_title("another_long_slug_here") == "Another Long Slug Here"


# --- API Tests ---

def test_get_articles_index_success(mock_fs):
    """Tests that the index endpoint returns a correctly formatted list of articles."""
    # Arrange: Create some dummy articles in the mocked filesystem
    (mock_fs / "0002_second_article").mkdir()
    (mock_fs / "0002_second_article" / "README.md").touch()
    (mock_fs / "0001_first_article").mkdir()
    (mock_fs / "0001_first_article" / "README.md").touch()

    with TestClient(viewer_server.app) as client:
        # Act
        response = client.get("/api/articles")
        # Assert
        assert response.status_code == 200
        data = response.json()
        assert len(data) == 2
        # The list is sorted by name, so "0001_first_article" should be first
        assert data[0]["slug"] == "0001_first_article"
        assert data[0]["title"] == "First Article"
        assert data[1]["slug"] == "0002_second_article"
        assert data[1]["title"] == "Second Article"


def test_get_articles_index_empty_and_invalid(mock_fs):
    """
    Tests that the index is empty if no valid articles are found, ignoring
    directories without a README.md and standalone files.
    """
    # Arrange: Create invalid content
    (mock_fs / "no_readme_dir").mkdir()
    (mock_fs / "a_file.txt").touch()

    with TestClient(viewer_server.app) as client:
        # Act
        response = client.get("/api/articles")
        # Assert
        assert response.status_code == 200
        assert response.json() == []


def test_get_article_content_success(mock_fs):
    """Tests fetching the content of a valid article."""
    # Arrange
    article_dir = mock_fs / "0001_my_article"
    article_dir.mkdir()
    (article_dir / "README.md").write_text("# Hello World")

    with TestClient(viewer_server.app) as client:
        # Act
        response = client.get("/api/articles/0001_my_article")
        # Assert
        assert response.status_code == 200
        assert response.text == "# Hello World"
        assert response.headers["content-type"] == "text/markdown; charset=utf-8"


def test_get_article_content_not_found(mock_fs):
    """Tests that requesting a non-existent article slug returns a 404."""
    with TestClient(viewer_server.app) as client:
        response = client.get("/api/articles/non_existent_slug")
        assert response.status_code == 404


def test_serve_article_static_success(mock_fs):
    """Tests serving a static file from within an article's directory."""
    # Arrange
    article_dir = mock_fs / "0001_my_article"
    article_dir.mkdir()
    (article_dir / "README.md").touch()  # Must exist for the slug to be valid
    static_dir = article_dir / "static"
    static_dir.mkdir()
    (static_dir / "image.png").write_text("fake_image_data")

    with TestClient(viewer_server.app) as client:
        # Act
        response = client.get("/articles/0001_my_article/static/image.png")
        # Assert
        assert response.status_code == 200
        assert response.text == "fake_image_data"


def test_serve_article_static_article_not_found(mock_fs):
    """Tests that a static file request returns 404 for an invalid article slug."""
    with TestClient(viewer_server.app) as client:
        response = client.get("/articles/non_existent_slug/static/image.png")
        assert response.status_code == 404


def test_serve_article_static_file_not_found(mock_fs):
    """Tests that a static file request returns 404 for a valid slug but invalid file."""
    # Arrange
    article_dir = mock_fs / "0001_my_article"
    article_dir.mkdir()
    (article_dir / "README.md").touch()

    with TestClient(viewer_server.app) as client:
        # Act
        response = client.get("/articles/0001_my_article/static/image.png")
        # Assert
        assert response.status_code == 404


def test_serve_frontend(mock_fs):
    """
    Tests that the root path serves the exact content of the index.html file.
    """
    # Arrange: Find the real index.html and copy its content into our
    # isolated test environment.
    real_template_path = Path(__file__).parent / "templates" / "index.html"

    # Pre-condition: Ensure the real template file exists before running the test.
    if not real_template_path.is_file():
        pytest.skip("Real index.html template not found.")

    # Write the real content to the mocked template file.
    (viewer_server.TEMPLATES_DIR / "index.html").write_text(real_template_path.read_text())

    with TestClient(viewer_server.app) as client:
        # Act: Request the root page.
        response = client.get("/")

        # Assert: The response must be successful and the content must exactly
        # match the content of the real template file.
        assert response.status_code == 200
        assert response.text == real_template_path.read_text()